#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include "rang.hpp"

#include "../include/cpp_box/arm.hpp"
#include "../include/cpp_box/compiler.hpp"
#include "../include/cpp_box/memory_map.hpp"

template<typename Cont> void dump_rom(const Cont &c)
{
  std::size_t loc = 0;

  std::cerr << "Dumping Data At Loc: " << static_cast<const void *>(c.data()) << '\n';

  for (const auto byte : c) {
    std::cerr << std::setw(2) << std::setfill('0') << std::hex << static_cast<int>(byte) << ' ';
    if ((++loc % 16) == 0) { std::cerr << '\n'; }
  }
  std::cerr << '\n';
}

template<typename Map> void dump_memory_map(const Map &m)
{
  std::size_t loc = 0;
  for (const auto &memory : m) {
    std::cout << loc << ": " << memory.in_use << ' ' << std::hex << std::setfill('0') << static_cast<const void *>(memory.data) << ' ' << std::setw(8)
              << memory.start << ' ' << memory.end << '\n';
    ++loc;
  }
}

template<typename System, typename Registers> void dump_state(const System &sys, const Registers &last_registers)
{
  std::cout << ' ' << std::setw(8) << std::setfill('0') << std::hex << sys.PC();

  for (std::size_t reg = 0; reg < sys.registers.size(); ++reg) {
    if (sys.registers[reg] == last_registers[reg]) {
      std::cout << rang::style::dim;
    } else {
      std::cout << rang::style::reset;
    }
    std::cout << ' ' << std::setw(8) << std::setfill('0') << std::hex << sys.registers[reg];
  }

  std::cout << '\n';
}

int main(const int argc, const char *argv[]) // NOLINT
{
  std::vector<std::string> args{ argv, argv + argc };
  auto logger = spdlog::stdout_color_mt("console");


  if (args.size() == 2) {
    std::cerr << "Attempting to load file: " << args[1] << '\n';

    const auto loaded_files{ cpp_box::load_unknown(std::filesystem::path{ args[1] }, *logger) };

    auto sys = std::make_unique<cpp_box::arm::System<cpp_box::system::TOTAL_RAM, std::vector<std::uint8_t>>>(
      loaded_files.image, static_cast<std::uint32_t>(cpp_box::system::Memory_Map::USER_RAM_START));


    logger->trace("setting up registers");
    sys->write_word(static_cast<std::uint32_t>(cpp_box::system::Memory_Map::RAM_SIZE), cpp_box::system::TOTAL_RAM);
    sys->write_half_word(static_cast<std::uint32_t>(cpp_box::system::Memory_Map::SCREEN_WIDTH), 64);
    sys->write_half_word(static_cast<std::uint32_t>(cpp_box::system::Memory_Map::SCREEN_HEIGHT), 64);
    sys->write_byte(static_cast<std::uint32_t>(cpp_box::system::Memory_Map::SCREEN_BPP), 32);
    sys->write_word(static_cast<std::uint32_t>(cpp_box::system::Memory_Map::SCREEN_BUFFER), cpp_box::system::DEFAULT_SCREEN_BUFFER);
    //dump_rom(RAM);

//    auto last_registers = sys->registers;
    int opcount         = 0;
    const auto tracer =
      [&opcount]([[maybe_unused]] const auto & /*t_sys*/, [[maybe_unused]] const auto /*t_pc*/, [[maybe_unused]] const auto /*t_ins*/) { ++opcount; };

//    cpp_box::utility::runtime_assert(sys->SP() == cpp_box::system::STACK_START);
    sys->run(static_cast<std::uint32_t>(loaded_files.entry_point) + static_cast<std::uint32_t>(cpp_box::system::Memory_Map::USER_RAM_START), tracer);

    std::cout << "Total instructions executed: " << opcount << '\n';

    //dump_state(sys, last_registers);
    // if ((++opcount) % 1000 == 0) { std::cout << opcount << '\n'; }
  }
}
