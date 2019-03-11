#include "../include/cpp_box/arm.hpp"
#include "../include/cpp_box/elf_reader.hpp"
#include "../include/cpp_box/state_machine.hpp"
#include "../include/cpp_box/hardware.hpp"
#include "../include/cpp_box/utility.hpp"
#include "../include/cpp_box/compiler.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "../include/cpp_box/utility.hpp"

#include <clara.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>


int main(const int argc, const char *argv[])
{
  using clara::Opt;
  using clara::Arg;
  using clara::Args;
  using clara::Help;
  bool showHelp{ false };
  std::filesystem::path inputFile;
  std::filesystem::path outputFile;

  std::filesystem::path user_provided_clang;
  std::filesystem::path user_provided_freestanding_stdlib;
  std::filesystem::path user_provided_hardware_lib;

  auto cli = Help(showHelp) | Opt(user_provided_clang, "path")["--clang_compiler"]("compile C++ with <clang_compiler>")
             | Opt(user_provided_freestanding_stdlib, "path")["--freestanding_stdlib"]("freestanding stdlib implementation to use")
             | Opt(user_provided_hardware_lib, "path")["--hardware_lib"]("hardware lib implementation to use")
             | Opt(inputFile, "file")["--input"]("source file to compile") | Opt(outputFile, "file")["--output"]("object file to output");

  const auto result = cli.parse(Args(argc, argv));
  if (!result) {
    std::cerr << "Error in command line: " << result.errorMessage() << '\n';
    return EXIT_FAILURE;
  }

  if (showHelp) {
    std::cout << cli << '\n';
    return EXIT_SUCCESS;
  }

  const auto clang_compiler =
    cpp_box::find_clang(user_provided_clang, R"(C:\Program Files\LLVM\bin\clang++)", "/usr/local/bin/clang++", "/usr/bin/clang++");

  if (clang_compiler.empty()) {
    std::cerr << "Unable to locate a viable clang compiler\n";
    return EXIT_FAILURE;
  } else {
    std::cout << "Using compiler: '" << clang_compiler << "'\n";
  }

  const auto file_data = [](const auto &input) {
    return std::string{input.begin(), input.end()};
  };

  const auto compile_result = cpp_box::compile(file_data(cpp_box::utility::read_file(inputFile)),
                                               clang_compiler,
                                               user_provided_freestanding_stdlib,
                                               user_provided_hardware_lib,
                                               "3",
                                               "c++2a",
                                               *spdlog::stdout_color_mt("console"));


  cpp_box::utility::write_binary_file(outputFile, compile_result.image);
}
