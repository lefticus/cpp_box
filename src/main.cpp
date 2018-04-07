#include "arm_thing/arm.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <iomanip>

#include "rang.hpp"

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

int main(const int argc, const char *argv[])
{
  std::vector<std::string> args{ argv, argv + argc };

  if (args.size() == 2) {
    std::cerr << "Attempting to load file: " << args[1] << '\n';

    auto RAM = [&]() {
      if (std::ifstream ifs{ args[1], std::ios::binary }; ifs.good()) {
        const auto file_size = ifs.seekg(0, std::ios_base::end).tellg();
        ifs.seekg(0);
        std::vector<char> data;
        data.resize(static_cast<std::size_t>(file_size));
        ifs.read(data.data(), file_size);
        std::cerr << "Loaded file: '" << args[1] << "' of size: " << file_size << '\n';
        return std::vector<uint8_t>{ begin(data), end(data) };
      } else {
        std::cerr << "Error opening file: " << argv[1] << '\n';
        exit(EXIT_FAILURE);
      }
    }();

    ARM_Thing::System sys{};
    dump_rom(RAM);
    sys.set(RAM, 0x01000000, 0);

    dump_memory_map(sys.ram_map);
    dump_memory_map(sys.rom_map);


    auto last_registers = sys.registers;
    int opcount         = 0;
    const auto tracer   = [&opcount, &last_registers](const auto &sys, const auto pc, const auto ins) {
      //std::cout << opcount++;
      //std::cout << ' ' << std::setw(8) << std::setfill('0') << std::hex << pc << ' ' << ins.data();
      /*
      for (std::size_t reg = 0; reg < sys.registers.size(); ++reg) {
        if (sys.registers[reg] == last_registers[reg]) {
          std::cout << rang::style::dim;
        } else {
          std::cout << rang::style::reset;
        }
        std::cout <<' ' << std::setw(8) << std::setfill('0') << std::hex << sys.registers[reg];
      }
      */
      //last_registers = sys.registers;
      //std::cout << '\n';

            if ( (++opcount) % 1000 == 0) {
              std::cout << opcount++ << '\n';
            }
    };

    sys.run(0x01000000, tracer);
  }
}
