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


  static std::vector<uint8_t> read_file(const std::filesystem::path &t_path)
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
  static void resolve_symbols(std::vector<std::uint8_t> &data, arm_thing::elf::File_Header file_header, spdlog::logger &logger)
  {
    logger.info("Resolving symbols");
    const auto sh_string_table = file_header.sh_string_table();
    const auto string_table    = file_header.string_table();
    const auto symbol_table    = file_header.symbol_table();


    for (const auto &section_header : file_header.section_headers()) {
      if (section_header.type() == arm_thing::elf::Section_Header::Types::SHT_REL && section_header.name(sh_string_table) == ".rel.text") {
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

          if (arm_thing::System<>::decode(arm_thing::Instruction{ value }) == arm_thing::Instruction_Type::Branch) {
            const auto new_value = (value & 0xFF000000) | (((static_cast<std::int32_t>(to - from) >> 2) - 2) & 0x00FFFFFF);
            logger.info("Branch Instruction: {:#x} -> {:#x}", value, new_value);
            data[from]     = new_value;
            data[from + 1] = (new_value >> 8);
            data[from + 2] = (new_value >> 16);
            data[from + 3] = (new_value >> 24);
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

  static Loaded_Files load_unknown(const std::filesystem::path &t_path, spdlog::logger &logger)
  {
    auto data = std::make_unique<std::vector<std::uint8_t>>(read_file(t_path));
    logger.info("Loading unknown file type: '{}', file exists? {}", t_path.c_str(), std::filesystem::exists(t_path));

    if (data->size() >= 64) {
      const auto file_header = arm_thing::elf::File_Header{ { data->data(), data->size() } };
      logger.info("'{}' is ELF?: {}", t_path.c_str(), file_header.is_elf_file());
      if (file_header.is_elf_file()) {
        const auto string_header   = file_header.section_header(file_header.section_header_string_table_index());
        const auto sh_string_table = file_header.sh_string_table();

        const auto string_table = file_header.string_table();
        for (const auto &header : file_header.section_headers()) {
          for (const auto &symbol_table_entry : header.symbol_table_entries()) {
            if (symbol_table_entry.name(string_table) == "main") {
              resolve_symbols(*data, file_header, logger);
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

  // creates an RAII managed temporary directory
  // TODO: Put into reusable location
  struct Temp_Directory
  {
    Temp_Directory(const std::string_view t_prefix)
    {

      for (int count = 0; count < 1000; ++count) {
        const auto p = std::filesystem::temp_directory_path() / fmt::format("{}-{:04x}", t_prefix, count);
        if (std::filesystem::create_directories(p)) {
          m_dir = p;
          return;
        }
      }
      abort();  // couldn't create dir
    }

    ~Temp_Directory() { std::filesystem::remove_all(m_dir); }

    const std::filesystem::path &dir() const { return m_dir; }

    Temp_Directory(Temp_Directory &&)      = delete;
    Temp_Directory(const Temp_Directory &) = delete;
    Temp_Directory &operator=(const Temp_Directory &) = delete;
    Temp_Directory &operator=(Temp_Directory &&) = delete;

  private:
    std::filesystem::path m_dir;
  };

  // TODO: Make optimization level, standard, strongly typed things
  static Loaded_Files compile(const std::string &t_str,
                              const std::filesystem::path &t_compiler,
                              const std::string_view t_optimization_level,
                              const std::string_view t_standard,
                              spdlog::logger &logger)
  {
    logger.info("Compile Starting");

    Temp_Directory dir("ARM_THING");

    logger.debug("Using dir: '{}'", dir.dir().c_str());
    const auto cpp_file = dir.dir() / "src.cpp";
    const auto asm_file = dir.dir() / "src.s";
    const auto obj_file = dir.dir() / "src.o";

    if (std::ofstream ofs(cpp_file); ofs.good()) {
      ofs.write(t_str.data(), t_str.find('\0'));
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
    const auto assembly = read_file(asm_file);
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
              return compile(src, "/usr/local/bin/clang++", "3", "c++2a", *console);
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
