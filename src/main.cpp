#include "xeus-flang-repl/xinterpreter.hpp"

#include "xeus-zmq/xserver_zmq.hpp"
#include "xeus-zmq/xzmq_context.hpp"
#include "xeus/xhelper.hpp"
#include "xeus/xkernel.hpp"
#include "xeus/xkernel_configuration.hpp"
#include "xeus/xlogger.hpp"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

std::string optionValue(int argc, char **argv, const std::string &name) {
  const std::string prefix = name + "=";
  for (int index = 1; index < argc; ++index) {
    std::string argument = argv[index];
    if (argument.rfind(prefix, 0) == 0)
      return argument.substr(prefix.size());
    if (argument == name && index + 1 < argc)
      return argv[index + 1];
  }
  return {};
}

std::vector<std::string> optionValues(int argc, char **argv,
                                      const std::string &name) {
  std::vector<std::string> values;
  const std::string prefix = name + "=";
  for (int index = 1; index < argc; ++index) {
    std::string argument = argv[index];
    if (argument.rfind(prefix, 0) == 0) {
      values.push_back(argument.substr(prefix.size()));
    } else if (argument == name && index + 1 < argc) {
      values.push_back(argv[++index]);
    }
  }
  return values;
}

bool hasOption(int argc, char **argv, const std::string &name) {
  for (int index = 1; index < argc; ++index)
    if (argv[index] == name)
      return true;
  return false;
}

} // namespace

int main(int argc, char **argv) {
  if (xeus::should_print_version(argc, argv)) {
    std::cout << "xflang " << XEUS_FLANG_VERSION << '\n';
    return 0;
  }

  xflang::InterpreterOptions options;
  options.executablePath = argv[0];
  options.resourceDirectory = optionValue(argc, argv, "--resource-dir");
  if (options.resourceDirectory.empty())
    options.resourceDirectory = XEUS_FLANG_DEFAULT_RESOURCE_DIR;
  options.runtimeLibrary = optionValue(argc, argv, "--runtime-library");
  options.targetTriple = optionValue(argc, argv, "--target");
  options.compilerArguments = optionValues(argc, argv, "--compiler-arg");
  options.preloadLibraries = optionValues(argc, argv, "--load-library");
  options.trace = hasOption(argc, argv, "--trace");
  options.captureOutput = !hasOption(argc, argv, "--no-capture");

  try {
    std::string connectionFile = xeus::extract_filename(argc, argv);
    auto interpreter =
        std::make_unique<xflang::Interpreter>(std::move(options));
    std::unique_ptr<xeus::xcontext> context = xeus::make_zmq_context();

    if (!connectionFile.empty()) {
      xeus::xkernel kernel(xeus::load_configuration(connectionFile),
                           xeus::get_user_name(), std::move(context),
                           std::move(interpreter), xeus::make_xserver_default,
                           xeus::make_in_memory_history_manager(),
                           xeus::make_console_logger(xeus::xlogger::msg_type));
      kernel.start();
    } else {
      xeus::xkernel kernel(xeus::get_user_name(), std::move(context),
                           std::move(interpreter), xeus::make_xserver_default,
                           xeus::make_in_memory_history_manager(),
                           xeus::make_console_logger(xeus::xlogger::msg_type));
      std::cout << xeus::get_start_message(kernel.get_config()) << '\n';
      kernel.start();
    }
  } catch (const std::exception &exception) {
    std::cerr << "xflang: " << exception.what() << '\n';
    return 1;
  }

  return 0;
}
