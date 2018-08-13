#include "../include/arm_thing/elf_reader.hpp"
#include <filesystem>
#include <iostream>
#include <fstream>

// todo: move to shared location
auto read_file(const std::filesystem::path &filename)
{
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

  const auto file_header = arm_thing::elf::File_Header({ data.data(), data.size() });

  std::cout << "is_elf_file: " << file_header.is_elf_file() << '\n';
  std::cout << "program_header_num_entries: " << file_header.program_header_num_entries() << '\n';
  std::cout << "section_header_num_entries: " << file_header.section_header_num_entries() << '\n';
  std::cout << "section_header_string_table_index: " << file_header.section_header_string_table_index() << '\n';

  const auto string_header   = file_header.section_header(file_header.section_header_string_table_index());
  const auto sh_string_table = file_header.sh_string_table();

  std::cout << "string_table_offset: " << string_header.offset() << '\n';
  std::cout << "string_table_name_offset: " << string_header.name_offset() << '\n';
  std::cout << "string_table_name: " << string_header.name(sh_string_table) << '\n';
  std::cout << "string_table_size: " << string_header.size() << '\n';

  std::cout << "Iterating Tables\n";
  const auto string_table = file_header.string_table();
  for (const auto &header : file_header.section_headers()) {
    std::cout << "  table name: " << header.name(sh_string_table) << " offset: " << header.offset() << " size: " << header.size()
              << " num symbol entries: " << header.symbol_table_num_entries() << '\n';

    for (const auto &symbol_table_entry : header.symbol_table_entries()) {
      std::cout << "    name_offset: " << symbol_table_entry.name_offset() << " symbol name: " << symbol_table_entry.name(string_table)
                << " symbol offset: " << symbol_table_entry.value() << " table index: " << symbol_table_entry.section_header_table_index() << '\n';
      if (symbol_table_entry.name(string_table) == "main") {
        std::cout << "FOUND MAIN!\n";
      }
    }
  }
}
