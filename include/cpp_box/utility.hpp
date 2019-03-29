#ifndef ARM_THING_UTILITY_HPP
#define ARM_THING_UTILITY_HPP

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <vector>

namespace spdlog {
class logger;
}  // namespace spdlog


namespace cpp_box::elf {
struct File_Header;
}  // namespace cpp_box::elf

namespace cpp_box::utility {

[[nodiscard]] std::vector<uint8_t> read_file(const std::filesystem::path &t_path);


template<typename CharType> void write_binary_file(const std::filesystem::path &t_path, std::basic_string_view<CharType> data)
{
  std::ofstream ofs{ t_path, std::ios_base::out | std::ios_base::binary | std::ios_base::trunc };

  // It's OK and defined behavior to observe an object via a pointer to `const char *`
  ofs.write(reinterpret_cast<const char *>(data.data()), static_cast<std::streamsize>(data.size() * sizeof(CharType) / sizeof(char))); // NOLINT
}

[[nodiscard]] std::tuple<int, std::string, std::string> make_system_call(const std::string &command);

constexpr inline void runtime_assert(bool condition)
{
  if (!condition) { abort(); }
}

void resolve_symbols(std::vector<std::uint8_t> &data, const cpp_box::elf::File_Header &file_header, spdlog::logger &logger);

// creates an RAII managed temporary directory
struct Temp_Directory
{
  explicit Temp_Directory(const std::string_view t_prefix = "arm_thing");
  ~Temp_Directory();

  const std::filesystem::path &dir() { return m_dir; }

  Temp_Directory(Temp_Directory &&)      = delete;
  Temp_Directory(const Temp_Directory &) = delete;
  Temp_Directory &operator=(const Temp_Directory &) = delete;
  Temp_Directory &operator=(Temp_Directory &&) = delete;

private:
  std::filesystem::path m_dir;
};
}  // namespace cpp_box::utility


#endif
