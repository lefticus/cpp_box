#include "../include/arm_thing/elf_reader.hpp"
#include <filesystem>
#include <iostream>
#include <fstream>

// todo: move to shared location
auto read_file(const std::filesystem::path &filename) {
  if (std::ifstream ifs{ filename, std::ios::binary }; ifs.good()) {
    const auto file_size = ifs.seekg(0, std::ios_base::end).tellg();
    ifs.seekg(0);
    std::vector<char> data;
    data.resize(static_cast<std::size_t>(file_size));
    ifs.read(data.data(), file_size);
    return std::vector<uint8_t>{ begin(data), end(data) };
  } else {
    return std::vector<uint8_t>{};
  }
}


int main(const int argc, const char *const argv[])
{
  const std::filesystem::path exec_name{ argv[0] };

  if (argc != 2) {
    std::cerr << "usage: " << exec_name << " <filename>\n";
    return EXIT_FAILURE;
  }

  const std::filesystem::path filename{ argv[1] };

  const auto data = read_file(filename);

  const auto file_header = arm_thing::ELF::File_Header(begin(data), end(data));

  std::cout << "is_elf_file: " << file_header.is_elf_file() << '\n';
  std::cout << "program_header_num_entries: " << file_header.program_header_num_entries() << '\n';
  std::cout << "section_header_num_entries: " << file_header.section_header_num_entries() << '\n';
  std::cout << "section_header_string_table_index: " << file_header.section_header_string_table_index() << '\n';

}
