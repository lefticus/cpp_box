#include "../include/cpp_box/arm.hpp"
#include "../include/cpp_box/elf_reader.hpp"
#include "../include/cpp_box/utility.hpp"

#include <fstream>
#include <spdlog/spdlog.h>

namespace cpp_box::utility {
std::vector<uint8_t> read_file(const std::filesystem::path &t_path)
{
  if (std::ifstream ifs{ t_path, std::ios::binary }; ifs.good()) {
    const auto file_size = ifs.seekg(0, std::ios_base::end).tellg();
    ifs.seekg(0);
    std::vector<char> data;
    data.resize(static_cast<std::size_t>(file_size));
    ifs.read(data.data(), file_size);
    return { begin(data), end(data) };
  } else {
    return {};
  }
}

void resolve_symbols(std::vector<std::uint8_t> &data, const cpp_box::elf::File_Header &file_header, spdlog::logger &logger)
{
  logger.info("Resolving symbols");
  const auto sh_string_table = file_header.sh_string_table();
  const auto string_table    = file_header.string_table();
  const auto symbol_table    = file_header.symbol_table();


  for (const auto &section_header : file_header.section_headers()) {
    if (section_header.type() == cpp_box::elf::Section_Header::Types::SHT_REL && section_header.name(sh_string_table) == ".rel.text") {
      logger.info("Found .rel.text section");

      const auto source_section = [&]() {
        const auto section_name_to_find = section_header.name(sh_string_table).substr(4);

        logger.info("Looking for matching text section '{}'", section_name_to_find);
        for (const auto &section : file_header.section_headers()) {
          if (section.name(sh_string_table) == section_name_to_find) { return section; }
        }

        logger.error("'{}' not found, cannot proceed", section_name_to_find);
        abort();
      }();

      // is a relocation section
      for (const auto &relocation : section_header.relocation_table_entries()) {
        const auto symbol         = symbol_table.symbol_table_entry(relocation.symbol());
        const auto target_section = file_header.section_header(symbol.section_header_table_index());
        const auto from           = relocation.file_offset() + source_section.offset();
        const auto to             = symbol.value() + target_section.offset();


        logger.info("Attempting to relocate '{}'@{} -> {} (Offset: {})", symbol.name(string_table), from, to, static_cast<std::int32_t>(to - from));

        const auto value = static_cast<std::uint32_t>(data[from]) | (static_cast<std::uint32_t>(data[from + 1]) << 8)
                           | (static_cast<std::uint32_t>(data[from + 2]) << 16) | (static_cast<std::uint32_t>(data[from + 3]) << 24);

        if (cpp_box::arm::System<>::decode(cpp_box::arm::Instruction{ value }) == cpp_box::arm::Instruction_Type::Branch) {
          const auto new_value = (value & 0xFF000000) | (((static_cast<std::int32_t>(to - from) >> 2) - 2) & 0x00FFFFFF);
          logger.info("Branch Instruction: {:#x} -> {:#x}", value, new_value);
          data[from]     = static_cast<uint8_t>(new_value & 0xFF);
          data[from + 1] = static_cast<uint8_t>((new_value >> 8) & 0xFF);
          data[from + 2] = static_cast<uint8_t>((new_value >> 16) & 0xFF);
          data[from + 3] = static_cast<uint8_t>((new_value >> 24) & 0xFF);
        } else if (value == 0) {
          logger.info("Instruction is '0', nothing to link");
        } else {
          logger.error("Unhandled instruction: {0:#x}");
          abort();
        }
      }
    }
  }
}

// creates an RAII managed temporary directory
Temp_Directory::Temp_Directory(const std::string_view t_prefix)
{

  for (int count = 0; count < 1000; ++count) {
    const auto p = std::filesystem::temp_directory_path() / fmt::format("{}-{}-{:04x}", t_prefix, std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()), count);
    if (std::filesystem::create_directories(p)) {
      m_dir = p;
      return;
    }
  }
  abort();  // couldn't create dir
}

Temp_Directory::~Temp_Directory()
{
  std::filesystem::remove_all(m_dir);
}

}  // namespace cpp_box
