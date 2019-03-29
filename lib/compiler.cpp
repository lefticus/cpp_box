#include <regex>
#include <spdlog/spdlog.h>


#include <fstream>
#include <sstream>
#include <string>

#include "../include/cpp_box/compiler.hpp"
#include "../include/cpp_box/elf_reader.hpp"
#include "../include/cpp_box/print_utilities.hpp"
#include "../include/cpp_box/utility.hpp"

namespace cpp_box {

std::pair<bool, std::string> test_clang(const std::filesystem::path &p)
{
  if (std::error_code ec{}; std::filesystem::is_regular_file(p, ec)) {
    if (const auto [result, out, err] = cpp_box::utility::make_system_call(fmt::format("{} --version", p.string()));
        out.find("clang") != std::string::npos) {
      return { true, out.substr(0, out.find('\n')) };
      ;
    }
  }

  return { false, "" };
}

Loaded_Files load_unknown(const std::filesystem::path &t_path, spdlog::logger &logger)
{
  auto data = std::make_unique<std::vector<std::uint8_t>>(cpp_box::utility::read_file(t_path));
  logger.info("Loading unknown file type: '{}', file exists? {}", t_path.string(), std::filesystem::exists(t_path));

  if (data->size() >= 64) {
    const auto file_header = cpp_box::elf::File_Header{ { data->data(), data->size() } };
    logger.info("'{}' is ELF?: {}", t_path.string(), file_header.is_elf_file());
    if (file_header.is_elf_file()) {
      const auto sh_string_table = file_header.sh_string_table();

      // TODO: make this local map better
      std::map<std::string, std::uint64_t> section_offsets;

      for (const auto &header : file_header.section_headers()) {
        const auto header_name       = std::string{ header.name(sh_string_table) };
        const auto offset            = header.offset();
        section_offsets[header_name] = offset;
        logger.trace("Section: '{}', offset: {}", header_name, offset);
      }

      const auto string_table = file_header.string_table();
      for (const auto &header : file_header.section_headers()) {
        for (const auto &symbol_table_entry : header.symbol_table_entries()) {
          if (symbol_table_entry.name(string_table) == "main") {
            cpp_box::utility::resolve_symbols(*data, file_header, logger);
            std::basic_string_view<std::uint8_t> data_view{ data->data(), data->size() };
            const auto main_section     = file_header.section_header(symbol_table_entry.section_header_table_index());
            const auto main_file_offset = static_cast<std::uint32_t>(main_section.offset() + symbol_table_entry.value());
            logger.info(
              "'main' symbol found in '{}':{} file offset: {}", main_section.name(sh_string_table), symbol_table_entry.value(), main_file_offset);
            return Loaded_Files{ "", "", std::move(data), data_view, main_file_offset, true, {}, section_offsets };
          }
        }
      }
    }
  }

  // if we make it here it's not an elf file or doesn't have main, assuming a
  // src file
  logger.info("Didn't find a main, assuming C++ src file");

  return { std::string{ data->begin(), data->end() }, "", {}, {}, {}, false, {}, {} };
}


// TODO: Make optimization level, standard, strongly typed things
Loaded_Files compile(const std::string &t_str,
                     const std::filesystem::path &t_clang_compiler,
                     const std::filesystem::path &t_freestanding_stdlib,
                     const std::filesystem::path &t_hardware_lib,
                     const std::string_view t_optimization_level,
                     const std::string_view t_standard,
                     spdlog::logger &logger)
{
  logger.info("Compile Starting");

  cpp_box::utility::Temp_Directory dir{};

  logger.debug("Using dir: '{}'", dir.dir().string());
  const auto cpp_file         = dir.dir() / "src.cpp";
  const auto asm_file         = dir.dir() / "src.s";
  const auto obj_file         = dir.dir() / "src.o";
  const auto disassembly_file = dir.dir() / "src.dis";

  if (std::ofstream ofs(cpp_file); ofs.good()) {
    ofs.write(t_str.data(), static_cast<std::streamsize>(t_str.size()));
    ofs.flush();  // make sure OS flushes file before clang tries to load it
  }

  const auto build_command = fmt::format(
    R"("{}" -std={} "{}" -c -o "{}" -O{} -g -save-temps=obj --target=arm-none-elf -march=armv4 -mfpu=vfp -mfloat-abi=hard -nostdinc -I"{}" -I"{}" -I"{}" -D__ELF__ -D_LIBCPP_HAS_NO_THREADS)",
    t_clang_compiler.string(),
    std::string(t_standard),
    cpp_file.string(),
    obj_file.string(),
    std::string(t_optimization_level),
    (t_freestanding_stdlib / "include").string(),
    (t_freestanding_stdlib / "freestanding" / "include").string(),
    t_hardware_lib.string());

  logger.debug("Executing compile command: '{}'", build_command);
  [[maybe_unused]] const auto [result, output, error] = cpp_box::utility::make_system_call(build_command);
  const auto assembly                                 = cpp_box::utility::read_file(asm_file);
  auto loaded                                         = load_unknown(obj_file, logger);

  logger.debug("Compile stdout: '{}'", output);
  logger.debug("Compile stderr: '{}'", error);

  const auto disassemble_command = fmt::format(R"("{}" -disassemble -demangle -line-numbers -full-leading-addr -source "{}")",
                                               (t_clang_compiler.parent_path() / "llvm-objdump").string(),
                                               obj_file.string());
  logger.debug("Executing disassemble command: '{}'", disassemble_command);
  [[maybe_unused]] const auto [disassembly_result, disassembly, disassembly_error] = cpp_box::utility::make_system_call(disassemble_command);


  const std::regex strip_attributes{ R"(\n\s+\..*)", std::regex::ECMAScript };
//  cpp_box::utility::dump_rom(loaded.image);

  const auto parse_disassembly = [&logger](const std::string &file, const auto &section_offsets) {
    const std::regex read_disassembly{ R"(\s+([0-9a-f]+):\s+(..) (..) (..) (..) \t(.*))" };
    const std::regex read_section_name{ R"(^Disassembly of section (.*):$)" };
    const std::regex read_function_name{ R"(^(.*:)$)" };
    const std::regex read_line_number{ R"(^; (.*):([0-9]+)$)" };
    const std::regex read_source_code{ R"(^; (.*)$)" };

    std::stringstream ss{ file };

    std::unordered_map<std::uint32_t, Memory_Location> memory_locations;

    std::string current_function_name{};
    std::string current_section{};
    std::uint32_t current_offset{};
    std::string current_file_name{};
    int current_line_number{};
    std::string current_source_text{};

    for (std::string line; std::getline(ss, line);) {
      std::smatch results;
      if (std::regex_match(line, results, read_disassembly)) {
        logger.trace("Parsed disassembly line: (offset: '{}') '{}' '{}' '{}' '{}' '{}'",
                     results.str(1),
                     results.str(2),
                     results.str(3),
                     results.str(4),
                     results.str(5),
                     results.str(6));
        const auto b1    = static_cast<std::uint32_t>(std::stoi(results.str(2), nullptr, 16));
        const auto b2    = static_cast<std::uint32_t>(std::stoi(results.str(3), nullptr, 16));
        const auto b3    = static_cast<std::uint32_t>(std::stoi(results.str(4), nullptr, 16));
        const auto b4    = static_cast<std::uint32_t>(std::stoi(results.str(5), nullptr, 16));
        const auto value = (b4 << 24) | (b3 << 16) | (b2 << 8) | b1;

        current_offset = static_cast<std::uint32_t>(std::stoi(results.str(1), nullptr, 16));

        logger.trace("Disassembly: '{:08x}', '{}'", value, results.str(6));
        memory_locations[static_cast<std::uint32_t>(current_offset + section_offsets.at(current_section))] =
          Memory_Location{ results.str(6), current_file_name, current_line_number, current_section, current_function_name };

      } else if (std::regex_match(line, results, read_section_name)) {
        logger.trace("Entering binary section: '{}'", results.str(1));
        current_section = results.str(1);
      } else if (std::regex_match(line, results, read_line_number)) {
        logger.trace("Entering line: '{}':'{}'", results.str(1), results.str(2));
        current_file_name   = results.str(1);
        current_line_number = std::stoi(results.str(2));
      } else if (std::regex_match(line, results, read_source_code)) {
        logger.trace("Source line: '{}'", results.str(1));
        current_source_text = results.str(1);
      } else if (std::regex_match(line, results, read_function_name)) {
        logger.trace("Entering function: '{}'", results.str(1));
        current_function_name = results.str(1);
      }
    }
    return memory_locations;
  };


  return Loaded_Files{ t_str,
                       std::regex_replace(std::string{ assembly.begin(), assembly.end() }, strip_attributes, ""),
                       std::move(loaded.binary_file),
                       loaded.image,
                       static_cast<std::uint32_t>(loaded.entry_point),
                       loaded.good_binary,
                       parse_disassembly(disassembly, loaded.section_offsets),
                       loaded.section_offsets };
}
}  // namespace cpp_box
