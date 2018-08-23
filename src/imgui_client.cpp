#include "../include/arm_thing/arm.hpp"
#include "../include/arm_thing/elf_reader.hpp"
#include <cmath>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <regex>
#include <filesystem>
#include <future>

#include "rang.hpp"

#include "imgui/lib/imgui.h"
#include "imgui/lib/imgui-SFML.h"

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/Sprite.hpp>

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

struct Loaded_Files
{
  std::string src;
  std::string assembly;
  // ptr to ensure that the string_view into the image cannot be invalidated
  // todo: find a better option for this?
  std::unique_ptr<std::vector<std::uint8_t>> binary_file;
  std::basic_string_view<std::uint8_t> image;
  std::uint32_t entry_point{};
};

// todo: move loader into more reusable place
void resolve_symbols(std::vector<std::uint8_t> &data, arm_thing::elf::File_Header file_header)
{
  const auto sh_string_table = file_header.sh_string_table();
  const auto string_table    = file_header.string_table();
  const auto symbol_table    = file_header.symbol_table();


  for (const auto &section_header : file_header.section_headers()) {
    if (section_header.type() == arm_thing::elf::Section_Header::Types::SHT_REL && section_header.name(sh_string_table) == ".rel.text") {
      const auto source_section = [&]() {
        const auto section_name_to_find = section_header.name(sh_string_table).substr(4);

        std::cout << "looking for section: '" << section_name_to_find << "'\n";
        for (const auto &section : file_header.section_headers()) {
          if (section.name(sh_string_table) == section_name_to_find) { return section; }
        }

        abort();
      }();

      // is a relocation section
      for (const auto &relocation : section_header.relocation_table_entries()) {
        const auto symbol         = symbol_table.symbol_table_entry(relocation.symbol());
        const auto target_section = file_header.section_header(symbol.section_header_table_index());
        const auto from           = relocation.file_offset() + source_section.offset();
        const auto to             = symbol.value() + target_section.offset();

        std::cout << std::dec << "attempting relocation: " << from << " Needs to point to: " << to
                  << " offset: " << static_cast<std::int32_t>(to - from) << '\n';

        const auto value = static_cast<std::uint32_t>(data[from]) | (static_cast<std::uint32_t>(data[from + 1]) << 8)
                           | (static_cast<std::uint32_t>(data[from + 2]) << 16) | (static_cast<std::uint32_t>(data[from + 3]) << 24);

        std::cout << "Instruction: " << std::hex << value << '\n';
        std::cout << "distance / 2: " << std::hex << (static_cast<std::int32_t>(to - from) >> 2) << '\n';
        std::cout << "adjusted for PC: " << std::hex << (static_cast<std::int32_t>(to - from) >> 2) - 2 << '\n';

        if (arm_thing::System<>::decode(arm_thing::Instruction{ value }) == arm_thing::Instruction_Type::Branch) {
          const auto new_value = (value & 0xFF000000) | (((static_cast<std::int32_t>(to - from) >> 2) - 2) & 0x00FFFFFF);
          std::cout << "New instruction: " << std::hex << new_value << '\n';
          data[from]     = new_value;
          data[from + 1] = (new_value >> 8);
          data[from + 2] = (new_value >> 16);
          data[from + 3] = (new_value >> 24);
        } else if (value == 0) {
          std::cout << "0, nothing to link\n";
        } else {
          abort();
        }
      }
    }
  }
}

Loaded_Files load_unknown(const std::filesystem::path &t_path)
{
  auto data = std::make_unique<std::vector<std::uint8_t>>(read_file(t_path));

  if (data->size() >= 64) {
    const auto file_header = arm_thing::elf::File_Header{ { data->data(), data->size() } };
    std::cout << "is_elf_file: " << file_header.is_elf_file() << '\n';
    if (file_header.is_elf_file()) {
      const auto string_header   = file_header.section_header(file_header.section_header_string_table_index());
      const auto sh_string_table = file_header.sh_string_table();

      const auto string_table = file_header.string_table();
      for (const auto &header : file_header.section_headers()) {
        for (const auto &symbol_table_entry : header.symbol_table_entries()) {
          if (symbol_table_entry.name(string_table) == "main") {
            resolve_symbols(*data, file_header);
            std::basic_string_view<std::uint8_t> data_view{data->data(), data->size()};
            std::cout << "FOUND MAIN!\n";
            return Loaded_Files{ "",
                                 "",
                                 std::move(data),
                                 data_view,
                                 static_cast<std::uint32_t>(file_header.section_header(symbol_table_entry.section_header_table_index()).offset()
                                                            + symbol_table_entry.value()) };
          }
        }
      }
    }
  }

  // if we make it here it's not an elf file or doesn't have main, assuming a src file
  std::cout << "DIDN'T FIND MAIN! ASSUMING SRC\n";

  return { std::string{ data->begin(), data->end() }, "", {}, {}, {} };
}

Loaded_Files compile(const std::string &t_str)
{
  // todo: proper temp directory and name output
  if (std::ofstream ofs("/tmp/src.cpp"); ofs.good()) {
    ofs.write(t_str.data(), t_str.find('\0'));
    ofs.flush();
  }

  system("/usr/local/bin/clang++ -std=c++2a /tmp/src.cpp -S -o /tmp/src.asm -O1 -mllvm --x86-asm-syntax=intel --target=armv4-linux -stdlib=libc++");
  const auto assembly = read_file("/tmp/src.asm");


  // get object file
  system("/usr/local/bin/clang++ -std=c++2a /tmp/src.cpp -c -o /tmp/src.o -O1 --target=armv4-linux -stdlib=libc++");
  auto loaded = load_unknown("/tmp/src.o");

  const std::regex strip_attributes{ "\\n\\s+\\..*", std::regex::ECMAScript };

  dump_rom(loaded.image);

  return Loaded_Files{ t_str,
                       std::regex_replace(std::string{ assembly.begin(), assembly.end() }, strip_attributes, ""),
                       std::move(loaded.binary_file),
                       loaded.image,
                       static_cast<std::uint32_t>(loaded.entry_point) };
}

int main(const int argc, const char *argv[])
{
  std::vector<std::string> args{ argv, argv + argc };

  Loaded_Files loaded_files{};

  if (args.size() == 2) { loaded_files = load_unknown(args[1]); }

  arm_thing::System<1024 * 1024> sys{ loaded_files.image };
  //    dump_rom(Loaded_Files.image);

  constexpr auto FPS = 30;

  auto scale_factor        = 1.0f;
  auto sprite_scale_factor = 3.0f;


  ImGui::CreateContext();
  sf::RenderWindow window(sf::VideoMode(1024, 768), "ARM Thing");
  window.setFramerateLimit(FPS);


  ImGui::SFML::Init(window);

  sf::Texture texture;
  if (!texture.create(256, 256)) return -1;

  //  texture.create(255,100);
  // Create a sprite that will display the texture
  sf::Sprite sprite(texture);
  sf::Clock framerateClock;
  sf::Clock deltaClock;
  constexpr const auto opsPerFrame = 10000000 / FPS;

  auto rescale_display = [&](const auto last_scale_factor) {
    ImGui::GetStyle().ScaleAllSizes(scale_factor / last_scale_factor);
    sprite.setScale(scale_factor * sprite_scale_factor, scale_factor * sprite_scale_factor);
    ImGui::GetIO().FontGlobalScale = scale_factor;
  };

  rescale_display(1.0f);

  bool paused   = true;
  bool step_one = false;

  std::future<Loaded_Files> future_compiler_output;
  bool compile_needed = true;

  while (window.isOpen()) {
    if (future_compiler_output.valid() && future_compiler_output.wait_for(std::chrono::milliseconds(1)) == std::future_status::ready) {
      loaded_files = future_compiler_output.get();
    }

    sf::Event event;
    while (window.pollEvent(event)) {
      ImGui::SFML::ProcessEvent(event);

      if (event.type == sf::Event::Closed) { window.close(); }
    }

    if (!paused) {
      for (int i = 0; i < opsPerFrame; ++i) { sys.next_operation(); }
    } else if (step_one) {
      sys.next_operation();
      step_one = false;
    }


    ImGui::SFML::Update(window, deltaClock.restart());

    texture.update(&sys.builtin_ram[0x10000]);

    ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    {
      if (paused) {
        paused = !ImGui::Button("Run");
      } else {
        paused = ImGui::Button("Stop");
      }
      ImGui::SameLine();
      step_one = ImGui::Button("Step");
      ImGui::SameLine();
      ImGui::Button("Continuously Step");
      if (ImGui::IsItemActive()) { step_one = true; }

      ImGui::SameLine();
      if (ImGui::Button("Reset")) {
        sys = decltype(sys){ loaded_files.image };
        sys.setup_run(loaded_files.entry_point);
      }

      const auto last_scale_factor        = scale_factor;
      const auto last_sprite_scale_factor = sprite_scale_factor;
      ImGui::InputFloat("Zoom", &scale_factor, 0.5f, 0.0f, 1);
      ImGui::InputFloat("Output Zoom", &sprite_scale_factor, 0.5f, 0.0f, 1);
      const auto elapsedSeconds = framerateClock.restart().asSeconds();
      ImGui::Text("%02.2f FPS ~%02.2f Mhz", 1 / elapsedSeconds, opsPerFrame / elapsedSeconds / 1000000);
      if (scale_factor != last_scale_factor || sprite_scale_factor != last_sprite_scale_factor) {
        scale_factor        = std::clamp(scale_factor, 1.0f, 4.0f);
        sprite_scale_factor = std::clamp(sprite_scale_factor, 1.0f, 5.0f);
        rescale_display(last_scale_factor);
      }
    }
    ImGui::End();


    ImGui::Begin("Screen", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    {
      ImGui::Image(sprite);
    }
    ImGui::End();

    ImGui::Begin("Registers", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    {
      ImGui::Text("R0 %08x R1 %08x R2  %08x R3  %08x R4  %08x R5 %08x R6 %08x R7 %08x",
                  sys.registers[0],
                  sys.registers[1],
                  sys.registers[2],
                  sys.registers[3],
                  sys.registers[4],
                  sys.registers[5],
                  sys.registers[6],
                  sys.registers[7]);

      ImGui::Text("R8 %08x R9 %08x R10 %08x R11 %08x R12 %08x SP %08x LR %08x PC %08x",
                  sys.registers[8],
                  sys.registers[9],
                  sys.registers[10],
                  sys.registers[11],
                  sys.registers[12],
                  sys.registers[13],
                  sys.registers[14],
                  sys.registers[15]);
      ImGui::Text("     NZCV                    IFT     ");
      ImGui::Text("CSPR %i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i",
                  arm_thing::test_bit(sys.CSPR, 31),
                  arm_thing::test_bit(sys.CSPR, 30),
                  arm_thing::test_bit(sys.CSPR, 29),
                  arm_thing::test_bit(sys.CSPR, 28),
                  arm_thing::test_bit(sys.CSPR, 27),
                  arm_thing::test_bit(sys.CSPR, 26),
                  arm_thing::test_bit(sys.CSPR, 25),
                  arm_thing::test_bit(sys.CSPR, 24),
                  arm_thing::test_bit(sys.CSPR, 23),
                  arm_thing::test_bit(sys.CSPR, 22),
                  arm_thing::test_bit(sys.CSPR, 21),
                  arm_thing::test_bit(sys.CSPR, 20),
                  arm_thing::test_bit(sys.CSPR, 19),
                  arm_thing::test_bit(sys.CSPR, 18),
                  arm_thing::test_bit(sys.CSPR, 17),
                  arm_thing::test_bit(sys.CSPR, 16),
                  arm_thing::test_bit(sys.CSPR, 15),
                  arm_thing::test_bit(sys.CSPR, 14),
                  arm_thing::test_bit(sys.CSPR, 13),
                  arm_thing::test_bit(sys.CSPR, 12),
                  arm_thing::test_bit(sys.CSPR, 11),
                  arm_thing::test_bit(sys.CSPR, 10),
                  arm_thing::test_bit(sys.CSPR, 9),
                  arm_thing::test_bit(sys.CSPR, 8),
                  arm_thing::test_bit(sys.CSPR, 7),
                  arm_thing::test_bit(sys.CSPR, 6),
                  arm_thing::test_bit(sys.CSPR, 5),
                  arm_thing::test_bit(sys.CSPR, 4),
                  arm_thing::test_bit(sys.CSPR, 3),
                  arm_thing::test_bit(sys.CSPR, 2),
                  arm_thing::test_bit(sys.CSPR, 1),
                  arm_thing::test_bit(sys.CSPR, 0));
    }

    ImGui::Text("Stack");
    const std::uint32_t stack_start = 0x8000;
    for (std::uint32_t idx = 0; idx < 40; idx += 4)
    {
      ImGui::Text("%08x: %08x", stack_start - idx, sys.read_word(stack_start - idx));
    }
    ImGui::End();


    ImGui::Begin("C++");
    {
      const auto available = ImGui::GetContentRegionAvail();
      if (loaded_files.src.size() - strlen(loaded_files.src.c_str()) < 256) { loaded_files.src.resize(loaded_files.src.size() + 512); }
      ImGui::BeginChild("Code", { available.x * 5 / 8, available.y });
      {
        if (ImGui::InputTextMultiline("", loaded_files.src.data(), loaded_files.src.size(), ImGui::GetContentRegionAvail())) {
          compile_needed = true;
        }

        if (compile_needed && !future_compiler_output.valid()) {
          future_compiler_output = std::async(std::launch::async, compile, std::string(loaded_files.src));
          compile_needed         = false;
        }
      }
      ImGui::EndChild();
      ImGui::SameLine();
      ImGui::BeginChild("Code Output", ImGui::GetContentRegionAvail());
      {
        ImGui::InputTextMultiline(
          "", loaded_files.assembly.data(), loaded_files.assembly.size(), ImGui::GetContentRegionAvail(), ImGuiInputTextFlags_ReadOnly);
      }
      ImGui::EndChild();
    }
    ImGui::End();

    window.clear();

    ImGui::SFML::Render(window);

    window.display();
  }

  ImGui::SFML::Shutdown();
  ImGui::DestroyContext();
}
