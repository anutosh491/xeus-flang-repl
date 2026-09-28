#ifndef XEUS_FLANG_REPL_XINTERPRETER_HPP
#define XEUS_FLANG_REPL_XINTERPRETER_HPP

#include "nlohmann/json.hpp"
#include "xeus/xinterpreter.hpp"
#include <memory>
#include <string>
#include <vector>

namespace xflang {} // namespace xflang

namespace Fortran::interpreter {
class Interpreter;
}

namespace xflang {

enum class CellCompilerMode {
  Isolated,
  Persistent,
};

struct InterpreterOptions {
  std::string executablePath;
  std::string resourceDirectory;
  std::string runtimeLibrary;
  std::string targetTriple;
  std::vector<std::string> compilerArguments;
  std::vector<std::string> preloadLibraries;
  CellCompilerMode cellCompilerMode{CellCompilerMode::Persistent};
  bool trace{false};
  bool captureOutput{true};
};

class Interpreter final : public xeus::xinterpreter {
public:
  explicit Interpreter(InterpreterOptions options);
  ~Interpreter() override;

private:
  void configure_impl() override;

  void execute_request_impl(send_reply_callback callback, int executionCounter,
                            const std::string &code,
                            xeus::execute_request_config config,
                            nlohmann::json userExpressions) override;

  nlohmann::json complete_request_impl(const std::string &code,
                                       int cursorPosition) override;
  nlohmann::json inspect_request_impl(const std::string &code,
                                      int cursorPosition,
                                      int detailLevel) override;
  nlohmann::json is_complete_request_impl(const std::string &code) override;
  nlohmann::json kernel_info_request_impl() override;
  nlohmann::json shutdown_request_impl(bool restart) override;
  nlohmann::json interrupt_request_impl() override;

  std::unique_ptr<Fortran::interpreter::Interpreter> interpreter;
  std::string runtimeLibraryPath;
  std::vector<std::string> preloadLibraries;
  void (*runtimeFlush)(int){nullptr};
  bool trace{false};
  bool captureOutput{true};
};

} // namespace xflang

#endif
