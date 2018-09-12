#include "../include/arm_thing/arm.hpp"
#include "../include/arm_thing/elf_reader.hpp"
#include "../include/arm_thing/utility.hpp"
#include "../include/arm_thing/state_machine.hpp"

#include <cmath>
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <regex>
#include <filesystem>
#include <random>
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
    bool good_binary{ false };
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
              return Loaded_Files{ "", "", std::move(data), data_view, main_file_offset, true };
            }
          }
        }
      }
    }

    // if we make it here it's not an elf file or doesn't have main, assuming a src file
    logger.info("Didn't find a main, assuming C++ src file");

    return { std::string{ data->begin(), data->end() }, "", {}, {}, {}, false };
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
                         static_cast<std::uint32_t>(loaded.entry_point),
                         loaded.good_binary };
  }

  struct Inputs
  {
    bool reset_pressed{ false };
    bool step_pressed{ false };
    bool source_changed{ false };
  };

  struct Status
  {
    enum class States { Static, Running, Begin_Build, Paused, Parse_Build_Results, Reset, Reset_Timer, Start, Step_One };

    spdlog::logger &m_logger;

    States current_state{ States::Start };
    float scale_factor{ 1.0 };
    float sprite_scale_factor{ 3.0 };
    bool paused{ false };
    sf::Clock framerateClock;
    sf::Clock timer;

    Loaded_Files loaded_files;

    bool build_good() const noexcept { return loaded_files.good_binary; }
    arm_thing::System<1024 * 1024> sys;

    sf::Texture texture;
    sf::Sprite sprite;

    static constexpr auto FPS               = 30;
    static constexpr const auto opsPerFrame = 10000000 / FPS;


    static constexpr auto state_machine = arm_thing::StateMachine{
      arm_thing::StateTransition{ States::Static,
                                  States::Static,
                                  [](const auto &status, const auto &) { return status.timer.getElapsedTime().asSeconds() < .5f; } },
      arm_thing::StateTransition{ States::Static,
                                  States::Running,
                                  [](const auto &status, const auto &) { return !status.paused && status.build_good(); } },
      arm_thing::StateTransition{ States::Static,
                                  States::Paused,
                                  [](const auto &status, const auto &) { return status.paused && status.build_good(); } },
      arm_thing::StateTransition{ States::Static,
                                  States::Begin_Build,
                                  [](const auto &status, const auto &inputs) { return status.needs_build || inputs.source_changed; } },
      arm_thing::StateTransition{ States::Begin_Build, States::Static, [](const auto &status, const auto &) { return !status.build_good(); } },
      arm_thing::StateTransition{ States::Begin_Build, States::Running, [](const auto &status, const auto &) { return status.build_good(); } },
      arm_thing::StateTransition{ States::Running,
                                  States::Begin_Build,
                                  [](const auto &status, const auto &inputs) { return status.needs_build || inputs.source_changed; } },
      arm_thing::StateTransition{ States::Running,
                                  States::Parse_Build_Results,
                                  [](const auto &status, const auto &) { return status.build_ready(); } },
      arm_thing::StateTransition{ States::Static,
                                  States::Parse_Build_Results,
                                  [](const auto &status, const auto &) { return status.build_ready(); } },
      arm_thing::StateTransition{ States::Paused,
                                  States::Parse_Build_Results,
                                  [](const auto &status, const auto &) { return status.build_ready(); } },
      arm_thing::StateTransition{ States::Running, States::Reset, [](const auto &, const auto &inputs) { return inputs.reset_pressed; } },
      arm_thing::StateTransition{ States::Paused, States::Running, [](const auto &status, const auto &) { return !status.paused; } },
      arm_thing::StateTransition{ States::Running, States::Paused, [](const auto &status, const auto &) { return status.paused; } },
      arm_thing::StateTransition{ States::Parse_Build_Results, States::Reset, [](const auto &, const auto &) { return true; } },
      arm_thing::StateTransition{ States::Reset, States::Reset_Timer, [](const auto &, const auto &) { return true; } },
      arm_thing::StateTransition{ States::Reset_Timer, States::Static, [](const auto &, const auto &) { return true; } },
      arm_thing::StateTransition{ States::Start, States::Reset, [](const auto &, const auto &) { return true; } },
      arm_thing::StateTransition{ States::Paused, States::Step_One, [](const auto &, const auto &inputs) { return inputs.step_pressed; } },
      arm_thing::StateTransition{ States::Step_One, States::Paused, [](const auto &, const auto &) { return true; } },
    };

    bool build_ready() const { return future_build.valid() && future_build.wait_for(std::chrono::microseconds(1)) == std::future_status::ready; }

    void reset()
    {
      sys = decltype(sys){ loaded_files.image };
      sys.setup_run(static_cast<std::uint32_t>(loaded_files.entry_point));
    }

    void reset_timer() { timer.restart(); }

    void rescale_display(const float new_scale_factor, const float new_sprite_scale_factor)
    {
      if (scale_factor != new_scale_factor || sprite_scale_factor != new_sprite_scale_factor) {
        const auto last_scale_factor{ scale_factor };
        scale_factor        = std::clamp(new_scale_factor, 1.0f, 4.0f);
        sprite_scale_factor = std::clamp(new_sprite_scale_factor, 1.0f, 5.0f);
        scale_impl(last_scale_factor);
      }
    }

    Status(spdlog::logger &logger, const std::filesystem::path &path)
      : m_logger{ logger }, loaded_files{ load_unknown(path, m_logger) }, sys{ loaded_files.image }
    {
      if (!texture.create(256, 256)) { abort(); }
      sprite.setTexture(texture);
      scale_impl(scale_factor);
    }

    States next_state(const Inputs inputs)
    {
      const auto last_state = current_state;
      current_state         = state_machine.transition(current_state, *this, inputs);
      if (last_state != current_state) { m_logger.debug("StateTransition {} -> {}", to_string(last_state), to_string(current_state)); }
      return current_state;
    }

    std::string to_string(const States state)
    {
      switch (state) {
      case States::Static: return "Static";
      case States::Running: return "Running";
      case States::Begin_Build: return "Begin_Build";
      case States::Paused: return "Paused";
      case States::Parse_Build_Results: return "Parse_Build_Results";
      case States::Reset: return "Reset";
      case States::Reset_Timer: return "Reset_Timer";
      case States::Start: return "Start";
      case States::Step_One: return "Step_One";
      }
      return "Unknown";
    }

    std::future<Loaded_Files> future_build;
    bool needs_build{ true };


  private:
    void scale_impl(const float last_scale_factor)
    {
      ImGui::GetStyle().ScaleAllSizes(scale_factor / last_scale_factor);
      sprite.setScale(scale_factor * sprite_scale_factor, scale_factor * sprite_scale_factor);
      ImGui::GetIO().FontGlobalScale = scale_factor;
    };
  };


  Inputs draw_interface(Status &status)
  {
    Inputs inputs;

    ImGui::Begin("Controls", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    {
      if (status.paused) {
        status.paused = !ImGui::Button(" Run ");
      } else {
        status.paused = ImGui::Button("Pause");
      }

      ImGui::SameLine();
      inputs.step_pressed = ImGui::Button("Step");
      ImGui::SameLine();
      ImGui::Button("Continuously Step");
      inputs.step_pressed = inputs.step_pressed || ImGui::IsItemActive();

      ImGui::SameLine();
      inputs.reset_pressed = ImGui::Button("Reset");

      auto scale_factor        = status.scale_factor;
      auto sprite_scale_factor = status.sprite_scale_factor;
      ImGui::InputFloat("Zoom", &scale_factor, 0.5f, 0.0f, 1);
      ImGui::InputFloat("Output Zoom", &sprite_scale_factor, 0.5f, 0.0f, 1);
      const auto elapsedSeconds = status.framerateClock.restart().asSeconds();
      ImGui::Text(
        "%02.2f FPS ~%02.2f Mhz", static_cast<double>(1 / elapsedSeconds), static_cast<double>(status.opsPerFrame / elapsedSeconds / 1000000));

      status.rescale_display(scale_factor, sprite_scale_factor);
    }
    ImGui::End();


    ImGui::Begin("Screen", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    {
      ImGui::Image(status.sprite);
    }
    ImGui::End();

    ImGui::Begin("Registers", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    {
      ImGui::Text("R0 %08x R1 %08x R2  %08x R3  %08x R4  %08x R5 %08x R6 %08x R7 %08x",
                  status.sys.registers[0],
                  status.sys.registers[1],
                  status.sys.registers[2],
                  status.sys.registers[3],
                  status.sys.registers[4],
                  status.sys.registers[5],
                  status.sys.registers[6],
                  status.sys.registers[7]);

      ImGui::Text("R8 %08x R9 %08x R10 %08x R11 %08x R12 %08x SP %08x LR %08x PC %08x",
                  status.sys.registers[8],
                  status.sys.registers[9],
                  status.sys.registers[10],
                  status.sys.registers[11],
                  status.sys.registers[12],
                  status.sys.registers[13],
                  status.sys.registers[14],
                  status.sys.registers[15]);
      ImGui::Text("     NZCV                    IFT     ");
      ImGui::Text("CSPR %i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i",
                  arm_thing::test_bit(status.sys.CSPR, 31),
                  arm_thing::test_bit(status.sys.CSPR, 30),
                  arm_thing::test_bit(status.sys.CSPR, 29),
                  arm_thing::test_bit(status.sys.CSPR, 28),
                  arm_thing::test_bit(status.sys.CSPR, 27),
                  arm_thing::test_bit(status.sys.CSPR, 26),
                  arm_thing::test_bit(status.sys.CSPR, 25),
                  arm_thing::test_bit(status.sys.CSPR, 24),
                  arm_thing::test_bit(status.sys.CSPR, 23),
                  arm_thing::test_bit(status.sys.CSPR, 22),
                  arm_thing::test_bit(status.sys.CSPR, 21),
                  arm_thing::test_bit(status.sys.CSPR, 20),
                  arm_thing::test_bit(status.sys.CSPR, 19),
                  arm_thing::test_bit(status.sys.CSPR, 18),
                  arm_thing::test_bit(status.sys.CSPR, 17),
                  arm_thing::test_bit(status.sys.CSPR, 16),
                  arm_thing::test_bit(status.sys.CSPR, 15),
                  arm_thing::test_bit(status.sys.CSPR, 14),
                  arm_thing::test_bit(status.sys.CSPR, 13),
                  arm_thing::test_bit(status.sys.CSPR, 12),
                  arm_thing::test_bit(status.sys.CSPR, 11),
                  arm_thing::test_bit(status.sys.CSPR, 10),
                  arm_thing::test_bit(status.sys.CSPR, 9),
                  arm_thing::test_bit(status.sys.CSPR, 8),
                  arm_thing::test_bit(status.sys.CSPR, 7),
                  arm_thing::test_bit(status.sys.CSPR, 6),
                  arm_thing::test_bit(status.sys.CSPR, 5),
                  arm_thing::test_bit(status.sys.CSPR, 4),
                  arm_thing::test_bit(status.sys.CSPR, 3),
                  arm_thing::test_bit(status.sys.CSPR, 2),
                  arm_thing::test_bit(status.sys.CSPR, 1),
                  arm_thing::test_bit(status.sys.CSPR, 0));
    }

    ImGui::Text("Stack");
    const std::uint32_t stack_start = 0x8000;
    for (std::uint32_t idx = 0; idx < 40; idx += 4) { ImGui::Text("%08x: %08x", stack_start - idx, status.sys.read_word(stack_start - idx)); }
    ImGui::End();


    ImGui::Begin("C++");
    {
      const auto available = ImGui::GetContentRegionAvail();
      if (status.loaded_files.src.size() - strlen(status.loaded_files.src.c_str()) < 256) {
        status.loaded_files.src.resize(status.loaded_files.src.size() + 512);
      }
      ImGui::BeginChild("Code", { available.x * 5 / 8, available.y });
      {
        inputs.source_changed =
          ImGui::InputTextMultiline("", status.loaded_files.src.data(), status.loaded_files.src.size(), ImGui::GetContentRegionAvail());
      }
      ImGui::EndChild();
      ImGui::SameLine();
      ImGui::BeginChild("Code Output", ImGui::GetContentRegionAvail());
      {
        ImGui::InputTextMultiline(
          "", status.loaded_files.assembly.data(), status.loaded_files.assembly.size(), ImGui::GetContentRegionAvail(), ImGuiInputTextFlags_ReadOnly);
      }
      ImGui::EndChild();
    }
    ImGui::End();

    return inputs;
  }

  void event_loop(const std::filesystem::path &original_path)
  {

    std::uniform_int_distribution<std::uint8_t> distribution(0, 255);
    std::default_random_engine generator;


    console->set_level(spdlog::level::trace);
    console->set_pattern("[%Y-%m-%d %H:%M:%S %z] [%n] [%^%l%$] [thread %t] %v");
    console->info("ARM Thing Starting");
    console->info("Original Path: {}", original_path.c_str());


    ImGui::CreateContext();
    sf::RenderWindow window(sf::VideoMode(1024, 768), "ARM Thing");


    ImGui::SFML::Init(window);

    sf::Clock deltaClock;

    Status status{ *console, original_path };
    window.setFramerateLimit(status.FPS);

    while (window.isOpen()) {

      sf::Event event;
      while (window.pollEvent(event)) {
        ImGui::SFML::ProcessEvent(event);

        if (event.type == sf::Event::Closed) { window.close(); }
      }


      ImGui::SFML::Update(window, deltaClock.restart());

      switch (status.next_state(draw_interface(status))) {
      case Status::States::Running:
        for (int i = 0; i < status.opsPerFrame; ++i) { status.sys.next_operation(); }
        status.texture.update(&status.sys.builtin_ram[0x10000]);
        break;
      case Status::States::Begin_Build:
        status.future_build = std::async(std::launch::async, [console = this->console, src = status.loaded_files.src]() {
          // string is oversized to allow for a buffer for IMGUI, need to only compile the first part of it
          return compile(src.substr(0, src.find('\0')), "/usr/local/bin/clang++", "3", "c++2a", *console);
        });
        status.needs_build  = false;
        break;
      case Status::States::Parse_Build_Results:
        status.loaded_files = status.future_build.get();
        console->debug("Results Loaded");
        break;
      case Status::States::Paused: break;
      case Status::States::Reset:
        status.reset();
        status.texture.update(&status.sys.builtin_ram[0x10000]);
        break;
      case Status::States::Start: break;
      case Status::States::Reset_Timer: status.reset_timer(); break;
      case Status::States::Step_One:
        status.sys.next_operation();
        status.texture.update(&status.sys.builtin_ram[0x10000]);
        break;
      case Status::States::Static:
        const auto texture_size = status.texture.getSize();
        std::vector<std::uint8_t> data(texture_size.x * texture_size.y * 4);
        std::generate(data.begin(), data.end(), [&distribution, &generator]() { return distribution(generator); });
        status.texture.update(data.data());
        break;
      }

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
