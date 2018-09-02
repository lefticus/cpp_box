#include "../include/arm_thing/arm.hpp"
#include "../include/arm_thing/elf_reader.hpp"
#include "../include/arm_thing/utility.hpp"
#include <cmath>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <regex>
#include <filesystem>
#include <future>

#include "imgui/lib/imgui.h"
#include "imgui/lib/imgui-SFML.h"

#include "../include/arm_thing/utility.hpp"

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/Sprite.hpp>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <rang.hpp>

struct Thing
{
  template<typename Cont> static void dump_rom(const Cont &c)
  {
    std::size_t loc = 0;


    std::clog << rang::fg::yellow << fmt::format("Dumping Data At Loc: {}\n", static_cast<const void *>(c.data())) << rang::style::dim;

    for (const auto byte : c) {
      std::clog << fmt::format(" {:02x}", byte);
      if ((++loc % 16) == 0) { std::clog << '\n'; }
    }
    std::clog << '\n' << rang::style::reset << rang::fg::reset;
  }


  struct Loaded_Files
  {
    std::string src;
    std::string assembly;
    // ptr to ensure that the string_view into the image cannot be invalidated
    // todo: find a better option for this?
    std::unique_ptr<std::vector<std::uint8_t>> binary_file;
    std::basic_string_view<std::uint8_t> image;
    std::uint64_t entry_point{};
  };


  static Loaded_Files load_unknown(const std::filesystem::path &t_path, spdlog::logger &logger)
  {
    auto data = std::make_unique<std::vector<std::uint8_t>>(arm_thing::read_file(t_path));
    logger.info("Loading unknown file type: '{}', file exists? {}", t_path.c_str(), std::filesystem::exists(t_path));

    if (data->size() >= 64) {
      const auto file_header = arm_thing::elf::File_Header{ { data->data(), data->size() } };
      logger.info("'{}' is ELF?: {}", t_path.c_str(), file_header.is_elf_file());
      if (file_header.is_elf_file()) {
        const auto sh_string_table = file_header.sh_string_table();

        const auto string_table = file_header.string_table();
        for (const auto &header : file_header.section_headers()) {
          for (const auto &symbol_table_entry : header.symbol_table_entries()) {
            if (symbol_table_entry.name(string_table) == "main") {
              arm_thing::resolve_symbols(*data, file_header, logger);
              std::basic_string_view<std::uint8_t> data_view{ data->data(), data->size() };
              const auto main_section     = file_header.section_header(symbol_table_entry.section_header_table_index());
              const auto main_file_offset = static_cast<std::uint32_t>(main_section.offset() + symbol_table_entry.value());
              logger.info(
                "'main' symbol found in '{}':{} file offset: {}", main_section.name(sh_string_table), symbol_table_entry.value(), main_file_offset);
              return Loaded_Files{ "", "", std::move(data), data_view, main_file_offset };
            }
          }
        }
      }
    }

    // if we make it here it's not an elf file or doesn't have main, assuming a src file
    logger.info("Didn't find a main, assuming C++ src file");

    return { std::string{ data->begin(), data->end() }, "", {}, {}, {} };
  }


  // TODO: Make optimization level, standard, strongly typed things
  static Loaded_Files compile(const std::string &t_str,
                              const std::filesystem::path &t_compiler,
                              const std::string_view t_optimization_level,
                              const std::string_view t_standard,
                              spdlog::logger &logger)
  {
    logger.info("Compile Starting");

    arm_thing::Temp_Directory dir("ARM_THING");

    logger.debug("Using dir: '{}'", dir.dir().c_str());
    const auto cpp_file = dir.dir() / "src.cpp";
    const auto asm_file = dir.dir() / "src.s";
    const auto obj_file = dir.dir() / "src.o";

    if (std::ofstream ofs(cpp_file); ofs.good()) {
      ofs.write(t_str.data(), static_cast<std::streamsize>(t_str.size()));
      ofs.flush();  // make sure OS flushes file before clang tries to load it
    }

    const auto build_command = fmt::format("{} -std={} {} -c -o {} -O{} -save-temps=obj --target=armv4-linux -stdlib=libc++",
                                           t_compiler.c_str(),
                                           t_standard,
                                           cpp_file.c_str(),
                                           obj_file.c_str(),
                                           t_optimization_level);
    std::system(build_command.c_str());

    logger.debug("Executing compile command: '{}'", build_command);
    const auto assembly = arm_thing::read_file(asm_file);
    auto loaded         = load_unknown(obj_file, logger);

    const std::regex strip_attributes{ "\\n\\s+\\..*", std::regex::ECMAScript };

    dump_rom(loaded.image);

    return Loaded_Files{ t_str,
                         std::regex_replace(std::string{ assembly.begin(), assembly.end() }, strip_attributes, ""),
                         std::move(loaded.binary_file),
                         loaded.image,
                         static_cast<std::uint32_t>(loaded.entry_point) };
  }

  void event_loop(const std::filesystem::path &original_path)
  {
    console->set_level(spdlog::level::trace);
    console->set_pattern("[%Y-%m-%d %H:%M:%S %z] [%n] [%^%l%$] [thread %t] %v");
    console->info("ARM Thing Starting");
    console->info("Original Path: {}", original_path.c_str());

    auto loaded_files = load_unknown(original_path, *console);
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
    if (!texture.create(256, 256)) { abort(); }

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
          sys.setup_run(static_cast<std::uint32_t>(loaded_files.entry_point));
        }

        const auto last_scale_factor        = scale_factor;
        const auto last_sprite_scale_factor = sprite_scale_factor;
        ImGui::InputFloat("Zoom", &scale_factor, 0.5f, 0.0f, 1);
        ImGui::InputFloat("Output Zoom", &sprite_scale_factor, 0.5f, 0.0f, 1);
        const auto elapsedSeconds = framerateClock.restart().asSeconds();
        ImGui::Text("%02.2f FPS ~%02.2f Mhz", static_cast<double>(1 / elapsedSeconds), static_cast<double>(opsPerFrame / elapsedSeconds / 1000000));
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
      for (std::uint32_t idx = 0; idx < 40; idx += 4) { ImGui::Text("%08x: %08x", stack_start - idx, sys.read_word(stack_start - idx)); }
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
            future_compiler_output = std::async(std::launch::async, [console = this->console, src = loaded_files.src]() {
              // string is oversized to allow for a buffer for IMGUI, need to only compile the first part of it
              return compile(src.substr(0, src.find('\0')), "/usr/local/bin/clang++", "3", "c++2a", *console);
            });
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

  std::shared_ptr<spdlog::logger> console{ spdlog::stdout_color_mt("console") };
};


int main(const int argc, const char *argv[])
{
  Thing t;

  t.event_loop(argc == 2 ? argv[1] : "");
}
