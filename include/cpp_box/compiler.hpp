#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "utility.hpp"

namespace spdlog {
class logger;
} // namespace spdlog

namespace cpp_box {

struct Memory_Location
{
  std::string disassembly;
  std::filesystem::path filename;
  int line_number{};
  std::string section;
  std::string function_name;
};


struct Loaded_Files
{
  std::string src;
  std::string assembly;
  // ptr to ensure that the string_view into the image cannot be invalidated
  // todo: find a better option for this?
  std::unique_ptr<std::vector<std::uint8_t>> binary_file{};
  std::basic_string_view<std::uint8_t> image{};
  std::uint64_t entry_point{};
  bool good_binary{ false };
  std::unordered_map<std::uint32_t, Memory_Location> location_data;
  std::map<std::string, std::uint64_t> section_offsets;
};

Loaded_Files load_unknown(const std::filesystem::path &t_path, spdlog::logger &logger);


std::pair<bool, std::string> test_clang(const std::filesystem::path &p);

template<typename ... Param>
auto find_clang(const Param... location) {
  for (const auto &p : std::initializer_list<std::filesystem::path>{ location... }) {
    if (auto [found, id] = test_clang(p); found) {
//      std::cerr << "Found clang: " << id << '\n';
      return p;
    }
  }

  return std::filesystem::path{};
}


Loaded_Files compile(const std::string &t_str,
                     const std::filesystem::path &t_clang_compiler,
                     const std::filesystem::path &t_freestanding_stdlib,
                     const std::filesystem::path &t_hardware_lib,
                     const std::string_view t_optimization_level,
                     const std::string_view t_standard,
                     spdlog::logger &logger,
                     bool is_cpp_mode = true);

}  // namespace cpp_box
