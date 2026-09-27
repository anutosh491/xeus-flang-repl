#include "xeus-flang-repl/xinterpreter.hpp"

#include "flang/Frontend/CompilerInstance.h"
#include "flang/Interpreter/Interpreter.h"
#include "flang/Interpreter/MLIRIncrementalExecutor.h"
#include "flang/Support/Fortran-features.h"
#include "xeus/xbase64.hpp"
#include "xeus/xhelper.hpp"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ExecutionEngine/Orc/Shared/ExecutorAddress.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/TargetParser/Triple.h"
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

namespace {

extern "C" void xflangDisplayData(const char *mimeType, const char *data) {
  if (!mimeType || !data)
    return;
  nlohmann::json bundle = nlohmann::json::object();
  bundle[mimeType] = data;
  xeus::get_interpreter().display_data(
      std::move(bundle), nlohmann::json::object(), nlohmann::json::object());
}

extern "C" void xflangDisplayBytes(const char *mimeType, const char *data,
                                   std::size_t size) {
  if (!mimeType || !data)
    return;
  nlohmann::json bundle = nlohmann::json::object();
  bundle[mimeType] = xeus::base64encode(std::string(data, size));
  xeus::get_interpreter().display_data(
      std::move(bundle), nlohmann::json::object(), nlohmann::json::object());
}

extern "C" void xflangClearOutput(bool wait) {
  xeus::get_interpreter().clear_output(wait);
}

constexpr llvm::StringLiteral DisplayModule = R"fortran(
module xflang_display
  use, intrinsic :: iso_c_binding, only: c_bool, c_char, c_null_char, c_size_t
  implicit none

  interface
    subroutine xflang_display_data_c(mime_type, data) &
        bind(c, name="xflangDisplayData")
      import c_char
      character(kind=c_char), intent(in) :: mime_type(*)
      character(kind=c_char), intent(in) :: data(*)
    end subroutine

    subroutine xflang_clear_output_c(wait) &
        bind(c, name="xflangClearOutput")
      import c_bool
      logical(kind=c_bool), value, intent(in) :: wait
    end subroutine

    subroutine xflang_display_bytes_c(mime_type, data, size) &
        bind(c, name="xflangDisplayBytes")
      import c_char, c_size_t
      character(kind=c_char), intent(in) :: mime_type(*)
      character(kind=c_char), intent(in) :: data(*)
      integer(kind=c_size_t), value, intent(in) :: size
    end subroutine
  end interface

contains
  subroutine display_data(mime_type, data)
    character(len=*), intent(in) :: mime_type, data
    call xflang_display_data_c(trim(mime_type) // c_null_char, &
                               trim(data) // c_null_char)
  end subroutine

  subroutine clear_output(wait)
    logical, intent(in), optional :: wait
    logical(kind=c_bool) :: should_wait
    should_wait = .false._c_bool
    if (present(wait)) should_wait = wait
    call xflang_clear_output_c(should_wait)
  end subroutine

  subroutine display_bytes(mime_type, data)
    character(len=*), intent(in) :: mime_type, data
    call xflang_display_bytes_c(trim(mime_type) // c_null_char, data, &
                                int(len(data), kind=c_size_t))
  end subroutine
end module
)fortran";

std::string getIntrinsicModulePath(llvm::StringRef resourceDirectory,
                                   const llvm::Triple &triple) {
  llvm::SmallString<256> path{resourceDirectory};
  llvm::sys::path::append(path, "finclude", "flang", triple.str());
  return path.str().str();
}

std::string getRuntimeLibraryPath(llvm::StringRef resourceDirectory,
                                  const llvm::Triple &triple) {
  llvm::SmallString<256> path{resourceDirectory};
  llvm::sys::path::append(path, "lib");
  if (triple.isOSDarwin())
    llvm::sys::path::append(path, "darwin", "libflang_rt.runtime.dylib");
  else
    llvm::sys::path::append(path, triple.str(), "libflang_rt.runtime.so");
  return path.str().str();
}

std::string readFile(std::FILE *file) {
  std::string result;
  if (!file || std::fseek(file, 0, SEEK_SET) != 0)
    return result;
  char buffer[4096];
  while (std::size_t count = std::fread(buffer, 1, sizeof(buffer), file))
    result.append(buffer, count);
  return result;
}

struct CapturedStreams {
  std::string out;
  std::string err;
};

class StreamCapture {
public:
  StreamCapture() {
#ifndef _WIN32
    outFile = std::tmpfile();
    errFile = std::tmpfile();
    if (!outFile || !errFile)
      throw std::runtime_error("could not create output capture files");

    std::fflush(nullptr);
    savedOut = ::dup(STDOUT_FILENO);
    savedErr = ::dup(STDERR_FILENO);
    if (savedOut < 0 || savedErr < 0 ||
        ::dup2(::fileno(outFile), STDOUT_FILENO) < 0 ||
        ::dup2(::fileno(errFile), STDERR_FILENO) < 0) {
      restore();
      throw std::runtime_error("could not redirect kernel output");
    }
#endif
  }

  StreamCapture(const StreamCapture &) = delete;
  StreamCapture &operator=(const StreamCapture &) = delete;

  ~StreamCapture() {
    restore();
    if (outFile)
      std::fclose(outFile);
    if (errFile)
      std::fclose(errFile);
  }

  CapturedStreams finish() {
    llvm::outs().flush();
    llvm::errs().flush();
    std::fflush(nullptr);
    restore();
    return {readFile(outFile), readFile(errFile)};
  }

private:
  void restore() {
#ifndef _WIN32
    if (savedOut >= 0) {
      ::dup2(savedOut, STDOUT_FILENO);
      ::close(savedOut);
      savedOut = -1;
    }
    if (savedErr >= 0) {
      ::dup2(savedErr, STDERR_FILENO);
      ::close(savedErr);
      savedErr = -1;
    }
#endif
  }

  std::FILE *outFile{nullptr};
  std::FILE *errFile{nullptr};
  int savedOut{-1};
  int savedErr{-1};
};

} // namespace

namespace xflang {

Interpreter::Interpreter(InterpreterOptions options) {
  static std::once_flag llvmInitialization;
  std::call_once(llvmInitialization, [] {
    llvm::InitializeAllTargetInfos();
    llvm::InitializeAllTargets();
    llvm::InitializeAllTargetMCs();
    llvm::InitializeAllAsmParsers();
    llvm::InitializeAllAsmPrinters();
  });

  llvm::Triple triple{options.targetTriple.empty()
                          ? llvm::sys::getProcessTriple()
                          : llvm::Triple::normalize(options.targetTriple)};
  const std::string intrinsicModuleDirectory =
      getIntrinsicModulePath(options.resourceDirectory, triple);

  Fortran::interpreter::IncrementalCompilerBuilder builder;
  builder.setExecutablePath(options.executablePath);
  builder.setTargetTriple(triple.str());
  builder.addIntrinsicModuleDirectory(intrinsicModuleDirectory);
  for (const std::string &argument : options.compilerArguments)
    builder.addCompilerArgument(argument);

  auto compiler = builder.create();
  if (!compiler)
    throw std::runtime_error(llvm::toString(compiler.takeError()));
  if (options.trace)
    llvm::errs()
        << "[xflang] OpenMP enabled: "
        << (*compiler)->getInvocation().getFrontendOpts().features.IsEnabled(
               Fortran::common::LanguageFeature::OpenMP)
        << '\n';

  auto executor =
      std::make_unique<Fortran::interpreter::MLIRIncrementalExecutor>();
  auto created = Fortran::interpreter::Interpreter::create(std::move(*compiler),
                                                           std::move(executor));
  if (!created)
    throw std::runtime_error(llvm::toString(created.takeError()));
  interpreter = std::move(*created);
  trace = options.trace;
  captureOutput = options.captureOutput;

  runtimeLibraryPath =
      options.runtimeLibrary.empty()
          ? getRuntimeLibraryPath(options.resourceDirectory, triple)
          : std::move(options.runtimeLibrary);
  preloadLibraries = std::move(options.preloadLibraries);
}

Interpreter::~Interpreter() = default;

void Interpreter::configure_impl() {
  xeus::register_interpreter(this);

  if (!llvm::sys::fs::exists(runtimeLibraryPath))
    throw std::runtime_error("Flang runtime library was not found at '" +
                             runtimeLibraryPath + "'");
  if (llvm::Error error =
          interpreter->loadDynamicLibrary(runtimeLibraryPath.c_str()))
    throw std::runtime_error(llvm::toString(std::move(error)));
  for (const std::string &path : preloadLibraries) {
    if (!llvm::sys::fs::exists(path))
      throw std::runtime_error("preload library was not found at '" + path +
                               "'");
    if (llvm::Error error = interpreter->loadDynamicLibrary(path.c_str()))
      throw std::runtime_error(llvm::toString(std::move(error)));
  }

  if (llvm::Error error = interpreter->registerSymbol(
          "xflangDisplayData",
          llvm::orc::ExecutorAddr::fromPtr(&xflangDisplayData)))
    throw std::runtime_error(llvm::toString(std::move(error)));
  if (llvm::Error error = interpreter->registerSymbol(
          "xflangClearOutput",
          llvm::orc::ExecutorAddr::fromPtr(&xflangClearOutput)))
    throw std::runtime_error(llvm::toString(std::move(error)));
  if (llvm::Error error = interpreter->registerSymbol(
          "xflangDisplayBytes",
          llvm::orc::ExecutorAddr::fromPtr(&xflangDisplayBytes)))
    throw std::runtime_error(llvm::toString(std::move(error)));
  if (llvm::Error error = interpreter->compileAndExecute(DisplayModule))
    throw std::runtime_error("could not initialize xflang_display: " +
                             llvm::toString(std::move(error)));

  runtimeFlush = reinterpret_cast<void (*)(int)>(
      llvm::sys::DynamicLibrary::SearchForAddressOfSymbol("_FortranAFlush"));
  if (!runtimeFlush)
    throw std::runtime_error(
        "loaded the Flang runtime but could not resolve _FortranAFlush");
}

void Interpreter::execute_request_impl(send_reply_callback callback, int,
                                       const std::string &code,
                                       xeus::execute_request_config config,
                                       nlohmann::json) {
  llvm::Error executionError = llvm::Error::success();
  CapturedStreams streams;
  std::string mlirOutput;
  try {
    llvm::StringRef source{code};
    bool showMLIR = false;
    if (source.consume_front("%%mlir\r\n") || source.consume_front("%%mlir\n"))
      showMLIR = true;

    llvm::Expected<Fortran::interpreter::CellArtifact &> cell =
        interpreter->compile(source);
    if (!cell) {
      executionError = cell.takeError();
    } else {
      if (trace) {
        llvm::errs() << "[xflang] prepared cell:\n"
                     << cell->getCompiledSource() << "[xflang] LLVM MLIR:\n";
        cell->getModule().print(llvm::errs());
        llvm::errs() << '\n';
      }
      if (showMLIR) {
        llvm::raw_string_ostream stream{mlirOutput};
        cell->getModule().print(stream);
        stream.flush();
      } else if (captureOutput) {
        StreamCapture capture;
        executionError = interpreter->execute(*cell);
        runtimeFlush(-1);
        streams = capture.finish();
      } else {
        executionError = interpreter->execute(*cell);
        runtimeFlush(-1);
      }
    }
  } catch (const std::exception &exception) {
    executionError = llvm::createStringError(llvm::inconvertibleErrorCode(),
                                             exception.what());
  }

  if (!config.silent) {
    if (!streams.out.empty())
      publish_stream("stdout", streams.out);
    if (!streams.err.empty())
      publish_stream("stderr", streams.err);
    if (!mlirOutput.empty()) {
      nlohmann::json bundle = nlohmann::json::object();
      bundle["text/plain"] = std::move(mlirOutput);
      display_data(std::move(bundle), nlohmann::json::object(),
                   nlohmann::json::object());
    }
  }

  if (executionError) {
    std::string errorText = llvm::toString(std::move(executionError));
    std::vector<std::string> traceback;
    if (!streams.err.empty())
      traceback.push_back(streams.err);
    traceback.push_back(errorText);
    if (!config.silent)
      publish_execution_error("FortranError", errorText, traceback);
    callback(xeus::create_error_reply("FortranError", errorText, traceback));
    return;
  }

  callback(xeus::create_successful_reply());
}

nlohmann::json Interpreter::complete_request_impl(const std::string &,
                                                  int cursorPosition) {
  return xeus::create_complete_reply(nlohmann::json::array(), cursorPosition,
                                     cursorPosition);
}

nlohmann::json Interpreter::inspect_request_impl(const std::string &, int,
                                                 int) {
  return xeus::create_inspect_reply(false);
}

nlohmann::json Interpreter::is_complete_request_impl(const std::string &) {
  return xeus::create_is_complete_reply("complete");
}

nlohmann::json Interpreter::kernel_info_request_impl() {
  nlohmann::json helpLinks = nlohmann::json::array();
  helpLinks.push_back({{"text", "Flang documentation"},
                       {"url", "https://flang.llvm.org/docs/"}});
  return xeus::create_info_reply("xeus-flang-repl", XEUS_FLANG_VERSION,
                                 "Fortran", "Fortran", "text/x-fortran", ".f90",
                                 "", std::string{"text/x-fortran"}, "",
                                 "xeus-flang-repl", helpLinks);
}

nlohmann::json Interpreter::shutdown_request_impl(bool restart) {
  return xeus::create_shutdown_reply(restart);
}

nlohmann::json Interpreter::interrupt_request_impl() {
  return xeus::create_interrupt_reply();
}

} // namespace xflang
