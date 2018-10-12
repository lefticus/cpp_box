#include "../include/cpp_box/arm.hpp"
#include "../include/cpp_box/elf_reader.hpp"
#include "../include/cpp_box/utility.hpp"
#include "../include/cpp_box/state_machine.hpp"

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
#include <sstream>
#include <memory>

#include "imgui/lib/imgui.h"
#include "imgui/lib/imgui-SFML.h"

#include "../include/cpp_box/utility.hpp"

#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/Event.hpp>
#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/Sprite.hpp>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <rang.hpp>
#include <clara.hpp>

struct Box
{

  constexpr static std::uint32_t TOTAL_RAM = 1024 * 1024 * 10;  // 10 MB
  enum struct Memory_Map : std::uint32_t {
    REGISTER_START = 0x00000000,
    TOTAL_RAM      = REGISTER_START + 0x0000,
    SCREEN_WIDTH   = REGISTER_START + 0x0004,  // 16bit screen width
    SCREEN_HEIGHT  = REGISTER_START + 0x0006,  // 16bit screen height
    SCREEN_BPP     = REGISTER_START + 0x0008,  // 8bit screen bits per pixel. Bits are divided evenly across the color space with preference given for
    // the odd bit to green, then to blue. Special cases for 1 bpp and 2 bpp.
    // 1 bpp: black or white
    // 2 bpp: 2 levels of grey (0%, 33%, 66%, 100%)
    // 3 bpp: 1 bit red, 1 bit green, 1 bit blue. Possible colors: black, white, red, green, blue, yellow, cyan, magenta).
    // 4 bpp: 1 bit red, 2 bits green, 1 bit blue.
    // 5 bpp: 1 bit red, 2 bits green, 2 bits blue.
    // 6 bpp: 2 bits red, 2 bits green, 2 bits blue.
    // 7 bpp: 2 bits red, 3 bits green, 2 bits blue.
    // 8 bpp: 2 bits red, 3 bits green, 3 bits blue.
    // etc
    // 24 bpp: max value without alpha
    // 32 bpp: 24 + alpha
    // 0xA0009,  // 8bit screen refresh rate
    // 0xA000A,  // 8bit Horizontal aspect
    // 0xA000B,  // 8bit Vertical aspect
    SCREEN_BUFFER  = REGISTER_START + 0x000C,  // 32bit pointer to current framebuffer
    USER_RAM_START = REGISTER_START + 0x1000,  // leave more space for registers, this is where binaries will load
  };

  constexpr static std::uint32_t DEFAULT_SCREEN_BUFFER = TOTAL_RAM - (1024 * 1024 * 2);  // by default VRAM is 2 MB from top
  constexpr static std::uint32_t STACK_START           = TOTAL_RAM - 1;


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
    std::unordered_map<std::uint32_t, std::string> disassembly;
  };


  static Loaded_Files load_unknown(const std::filesystem::path &t_path, spdlog::logger &logger)
  {
    auto data = std::make_unique<std::vector<std::uint8_t>>(cpp_box::utility::read_file(t_path));
    logger.info("Loading unknown file type: '{}', file exists? {}", t_path.string(), std::filesystem::exists(t_path));

    if (data->size() >= 64) {
      const auto file_header = cpp_box::elf::File_Header{ { data->data(), data->size() } };
      logger.info("'{}' is ELF?: {}", t_path.string(), file_header.is_elf_file());
      if (file_header.is_elf_file()) {
        const auto sh_string_table = file_header.sh_string_table();

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
              return Loaded_Files{ "", "", std::move(data), data_view, main_file_offset, true, {} };
            }
          }
        }
      }
    }

    // if we make it here it's not an elf file or doesn't have main, assuming a src file
    logger.info("Didn't find a main, assuming C++ src file");

    return { std::string{ data->begin(), data->end() }, "", {}, {}, {}, false, {} };
  }


  // TODO: Make optimization level, standard, strongly typed things
  static Loaded_Files compile(const std::string &t_str,
                              const std::filesystem::path &t_compiler,
                              const std::string_view t_optimization_level,
                              const std::string_view t_standard,
                              spdlog::logger &logger)
  {
    logger.info("Compile Starting");

    cpp_box::utility::Temp_Directory dir("ARM_THING");

    logger.debug("Using dir: '{}'", dir.dir().string());
    const auto cpp_file         = dir.dir() / "src.cpp";
    const auto asm_file         = dir.dir() / "src.s";
    const auto obj_file         = dir.dir() / "src.o";
    const auto disassembly_file = dir.dir() / "src.dis";

    if (std::ofstream ofs(cpp_file); ofs.good()) {
      ofs.write(t_str.data(), static_cast<std::streamsize>(t_str.size()));
      ofs.flush();  // make sure OS flushes file before clang tries to load it
    }

    const auto quote_command = [](const std::string &str) {
#if defined(_MSC_VER)
      return '"' + str + '"';
#else
      return str;
#endif
    };

    const auto build_command = quote_command(fmt::format(R"("{}" -std={} "{}" -c -o "{}" -O{} -save-temps=obj --target=armv4-linux -stdlib=libc++)",
                                                         t_compiler.string(),
                                                         std::string(t_standard),
                                                         cpp_file.string(),
                                                         obj_file.string(),
                                                         std::string(t_optimization_level)));

    logger.debug("Executing compile command: '{}'", build_command);
    std::system(build_command.c_str());
    const auto assembly = cpp_box::utility::read_file(asm_file);
    auto loaded         = load_unknown(obj_file, logger);


    const auto disassemble_command = quote_command(fmt::format(
      R"("{}" -disassemble "{}" > "{}")", (t_compiler.parent_path() / "llvm-objdump").string(), obj_file.string(), disassembly_file.string()));
    logger.debug("Executing disassemble command: '{}'", disassemble_command);
    std::system(disassemble_command.c_str());


    const std::regex strip_attributes{ "\\n\\s+\\..*", std::regex::ECMAScript };
    dump_rom(loaded.image);

    const auto parse_disassembly = [&logger](const std::string &file) {
      const std::regex read_disassembly{ ".*\\t(..) (..) (..) (..) \\t(.*)" };
      std::stringstream ss{ file };
      std::unordered_map<std::uint32_t, std::string> disassembly;
      for (std::string line; std::getline(ss, line);) {
        if (std::smatch results; std::regex_match(line, results, read_disassembly)) {
          //          logger.trace(
          //            "Parsed disassembly line: '{}' '{}' '{}' '{}' '{}'", results.str(1), results.str(2), results.str(3), results.str(4),
          //            results.str(5));
          const auto b1    = static_cast<std::uint32_t>(std::stoi(results.str(1), nullptr, 16));
          const auto b2    = static_cast<std::uint32_t>(std::stoi(results.str(2), nullptr, 16));
          const auto b3    = static_cast<std::uint32_t>(std::stoi(results.str(3), nullptr, 16));
          const auto b4    = static_cast<std::uint32_t>(std::stoi(results.str(4), nullptr, 16));
          const auto value = (b4 << 24) | (b3 << 16) | (b2 << 8) | b1;
          logger.trace("Disassembly: '{:08x}', '{}'", value, results.str(5));
          disassembly[value] = results.str(5);
        }
      }
      return disassembly;
    };

    const auto disassembly = cpp_box::utility::read_file(disassembly_file);

    return Loaded_Files{ t_str,
                         std::regex_replace(std::string{ assembly.begin(), assembly.end() }, strip_attributes, ""),
                         std::move(loaded.binary_file),
                         loaded.image,
                         static_cast<std::uint32_t>(loaded.entry_point),
                         loaded.good_binary,
                         parse_disassembly(std::string(disassembly.begin(), disassembly.end())) };
  }

  struct Inputs
  {
    bool reset_pressed{ false };
    bool step_pressed{ false };
    bool source_changed{ false };
  };

  struct Goal;

  struct Status
  {
    enum class States { Static, Running, Begin_Build, Paused, Parse_Build_Results, Reset, Reset_Timer, Start, Step_One, Check_Goal };

    spdlog::logger &m_logger;

    States current_state{ States::Start };
    float scale_factor{ 2.0 };
    float sprite_scale_factor{ 3.0 };
    bool paused{ false };
    bool show_assembly{ false };
    sf::Clock framerateClock;

    /// TODO: move somewhere shared
    struct Timer
    {
      Timer(const float timeoutInSeconds) noexcept : timeout{ timeoutInSeconds } {}
      bool expired() const noexcept { return timer.getElapsedTime().asSeconds() >= timeout; }
      void reset() noexcept { timer.restart(); }

    private:
      sf::Clock timer;
      float timeout;
    };


    Loaded_Files loaded_files;
    Timer static_timer{ 0.5f };

    bool build_good() const noexcept { return loaded_files.good_binary; }
    std::unique_ptr<cpp_box::arm::System<TOTAL_RAM, std::vector<std::uint8_t>>> sys;
    std::vector<Goal> goals;
    std::size_t current_goal{ 0 };

    sf::Texture texture;
    sf::Sprite sprite;

    static constexpr auto FPS               = 30;
    static constexpr const auto opsPerFrame = 10000000 / FPS;

    static constexpr auto s_build_ready       = [](const auto &status, const auto &) { return status.build_ready(); };
    static constexpr auto s_running           = [](const auto &status, const auto &) { return !status.paused && status.build_good(); };
    static constexpr auto s_paused            = [](const auto &status, const auto &) { return status.paused && status.build_good(); };
    static constexpr auto s_failed            = [](const auto &status, const auto &) { return !status.build_good(); };
    static constexpr auto s_static_timer      = [](const auto &status, const auto &) { return !status.static_timer.expired(); };
    static constexpr auto s_can_start_build   = [](const auto &status, const auto &) { return status.needs_build && !status.is_building(); };
    static constexpr auto s_always_true       = [](const auto &, const auto &) { return true; };
    static constexpr auto s_reset_pressed     = [](const auto &, const auto &inputs) { return inputs.reset_pressed; };
    static constexpr auto s_step_pressed      = [](const auto &, const auto &inputs) { return inputs.step_pressed; };
    static constexpr auto s_goal_check_needed = [](const auto &status, const auto &) {
      return !status.goals[status.current_goal].completed && !status.sys->operations_remaining();
    };

    static constexpr auto state_machine =
      cpp_box::state_machine::StateMachine{ cpp_box::state_machine::StateTransition{ States::Start, States::Reset, s_always_true },
                                            cpp_box::state_machine::StateTransition{ States::Reset, States::Reset_Timer, s_always_true },
                                            cpp_box::state_machine::StateTransition{ States::Reset_Timer, States::Static, s_always_true },
                                            cpp_box::state_machine::StateTransition{ States::Static, States::Static, s_static_timer },
                                            cpp_box::state_machine::StateTransition{ States::Static, States::Running, s_running },
                                            cpp_box::state_machine::StateTransition{ States::Static, States::Paused, s_paused },
                                            cpp_box::state_machine::StateTransition{ States::Static, States::Begin_Build, s_can_start_build },
                                            cpp_box::state_machine::StateTransition{ States::Static, States::Parse_Build_Results, s_build_ready },
                                            cpp_box::state_machine::StateTransition{ States::Begin_Build, States::Static, s_failed },
                                            cpp_box::state_machine::StateTransition{ States::Begin_Build, States::Running, s_running },
                                            cpp_box::state_machine::StateTransition{ States::Begin_Build, States::Paused, s_paused },
                                            cpp_box::state_machine::StateTransition{ States::Running, States::Begin_Build, s_can_start_build },
                                            cpp_box::state_machine::StateTransition{ States::Running, States::Parse_Build_Results, s_build_ready },
                                            cpp_box::state_machine::StateTransition{ States::Running, States::Reset, s_reset_pressed },
                                            cpp_box::state_machine::StateTransition{ States::Running, States::Paused, s_paused },
                                            cpp_box::state_machine::StateTransition{ States::Running, States::Check_Goal, s_goal_check_needed },
                                            cpp_box::state_machine::StateTransition{ States::Paused, States::Reset, s_reset_pressed },
                                            cpp_box::state_machine::StateTransition{ States::Paused, States::Parse_Build_Results, s_build_ready },
                                            cpp_box::state_machine::StateTransition{ States::Paused, States::Running, s_running },
                                            cpp_box::state_machine::StateTransition{ States::Paused, States::Step_One, s_step_pressed },
                                            cpp_box::state_machine::StateTransition{ States::Paused, States::Begin_Build, s_can_start_build },
                                            cpp_box::state_machine::StateTransition{ States::Parse_Build_Results, States::Reset, s_always_true },
                                            cpp_box::state_machine::StateTransition{ States::Step_One, States::Step_One, s_step_pressed },
                                            cpp_box::state_machine::StateTransition{ States::Step_One, States::Check_Goal, s_goal_check_needed },
                                            cpp_box::state_machine::StateTransition{ States::Step_One, States::Paused, s_always_true },
                                            cpp_box::state_machine::StateTransition{ States::Check_Goal, States::Paused, s_always_true } };

    bool build_ready() const { return future_build.valid() && future_build.wait_for(std::chrono::microseconds(1)) == std::future_status::ready; }
    bool is_building() const { return future_build.valid(); }

    void reset()
    {
      m_logger.trace("reset()");
      sys = std::make_unique<decltype(sys)::element_type>(loaded_files.image);
      sys->setup_run(static_cast<std::uint32_t>(loaded_files.entry_point));
      assert(sys->SP() == STACK_START);
      m_logger.trace("setting up registers");
      sys->write_word(static_cast<std::uint32_t>(Memory_Map::TOTAL_RAM), TOTAL_RAM);
      sys->write_half_word(static_cast<std::uint32_t>(Memory_Map::SCREEN_WIDTH), 64);
      sys->write_half_word(static_cast<std::uint32_t>(Memory_Map::SCREEN_HEIGHT), 64);
      sys->write_byte(static_cast<std::uint32_t>(Memory_Map::SCREEN_BPP), 32);
      sys->write_word(static_cast<std::uint32_t>(Memory_Map::SCREEN_BUFFER), DEFAULT_SCREEN_BUFFER);
    }

    void reset_static_timer() { static_timer.reset(); }

    void rescale_display(const float new_scale_factor, const float new_sprite_scale_factor)
    {
      if (scale_factor != new_scale_factor || sprite_scale_factor != new_sprite_scale_factor) {
        const auto last_scale_factor{ scale_factor };
        scale_factor        = std::clamp(new_scale_factor, 1.0f, 4.0f);
        sprite_scale_factor = std::clamp(new_sprite_scale_factor, 1.0f, 5.0f);
        scale_impl(last_scale_factor);
      }
    }

    Status(spdlog::logger &logger, const std::filesystem::path &path, std::vector<Goal> t_goals)
      : m_logger{ logger }
      , loaded_files{ load_unknown(path, m_logger) }
      , sys{ std::make_unique<decltype(sys)::element_type>(loaded_files.image) }
      , goals{ std::move(t_goals) }
    {
      m_logger.trace("Creating Status Object");
      if (!texture.create(256, 256)) { abort(); }
      sprite.setTexture(texture);
      scale_impl(1.0f);
    }

    States next_state(const Inputs inputs)
    {
      const auto last_state = current_state;
      current_state         = state_machine.transition(current_state, *this, inputs);
      if (last_state != current_state) { m_logger.debug("StateTransition {} -> {}", to_string(last_state), to_string(current_state)); }
      return current_state;
    }

    void update_display()
    {
      sf::Vector2u size{ sys->read_half_word(static_cast<std::uint32_t>(Memory_Map::SCREEN_WIDTH)),
                         sys->read_half_word(static_cast<std::uint32_t>(Memory_Map::SCREEN_HEIGHT)) };
      if (size != texture.getSize()) {
        m_logger.trace("Resizing screen to {}, {}", size.x, size.y);
        texture.create(size.x, size.y);
        sprite.setTexture(texture, true);
      }

      const auto display_loc = sys->read_word(static_cast<std::uint32_t>(Memory_Map::SCREEN_BUFFER));
      if (TOTAL_RAM - display_loc >= size.x * size.y * 4) {
        texture.update(&sys->builtin_ram[display_loc]);
      } else {
        // write as many lines as we can if we're past the end of RAM
        const auto pixels_to_write = std::min(size.x * size.y, (TOTAL_RAM - display_loc) / 4);
        texture.update(&sys->builtin_ram[display_loc], 0, 0, size.x, pixels_to_write / size.x);
      }
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
      case States::Check_Goal: return "Check_Goal";
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

    ImGui::Begin("State", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    {
      ImGui::Text("R0 %08x R1 %08x R2  %08x R3  %08x R4  %08x R5 %08x R6 %08x R7 %08x",
                  status.sys->registers[0],
                  status.sys->registers[1],
                  status.sys->registers[2],
                  status.sys->registers[3],
                  status.sys->registers[4],
                  status.sys->registers[5],
                  status.sys->registers[6],
                  status.sys->registers[7]);

      ImGui::Text("R8 %08x R9 %08x R10 %08x R11 %08x R12 %08x SP %08x LR %08x PC %08x",
                  status.sys->registers[8],
                  status.sys->registers[9],
                  status.sys->registers[10],
                  status.sys->registers[11],
                  status.sys->registers[12],
                  status.sys->registers[13],
                  status.sys->registers[14],
                  status.sys->registers[15]);
      ImGui::Text("     NZCV                    IFT     ");
      ImGui::Text("CSPR %i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i%i",
                  cpp_box::arm::test_bit(status.sys->CSPR, 31),
                  cpp_box::arm::test_bit(status.sys->CSPR, 30),
                  cpp_box::arm::test_bit(status.sys->CSPR, 29),
                  cpp_box::arm::test_bit(status.sys->CSPR, 28),
                  cpp_box::arm::test_bit(status.sys->CSPR, 27),
                  cpp_box::arm::test_bit(status.sys->CSPR, 26),
                  cpp_box::arm::test_bit(status.sys->CSPR, 25),
                  cpp_box::arm::test_bit(status.sys->CSPR, 24),
                  cpp_box::arm::test_bit(status.sys->CSPR, 23),
                  cpp_box::arm::test_bit(status.sys->CSPR, 22),
                  cpp_box::arm::test_bit(status.sys->CSPR, 21),
                  cpp_box::arm::test_bit(status.sys->CSPR, 20),
                  cpp_box::arm::test_bit(status.sys->CSPR, 19),
                  cpp_box::arm::test_bit(status.sys->CSPR, 18),
                  cpp_box::arm::test_bit(status.sys->CSPR, 17),
                  cpp_box::arm::test_bit(status.sys->CSPR, 16),
                  cpp_box::arm::test_bit(status.sys->CSPR, 15),
                  cpp_box::arm::test_bit(status.sys->CSPR, 14),
                  cpp_box::arm::test_bit(status.sys->CSPR, 13),
                  cpp_box::arm::test_bit(status.sys->CSPR, 12),
                  cpp_box::arm::test_bit(status.sys->CSPR, 11),
                  cpp_box::arm::test_bit(status.sys->CSPR, 10),
                  cpp_box::arm::test_bit(status.sys->CSPR, 9),
                  cpp_box::arm::test_bit(status.sys->CSPR, 8),
                  cpp_box::arm::test_bit(status.sys->CSPR, 7),
                  cpp_box::arm::test_bit(status.sys->CSPR, 6),
                  cpp_box::arm::test_bit(status.sys->CSPR, 5),
                  cpp_box::arm::test_bit(status.sys->CSPR, 4),
                  cpp_box::arm::test_bit(status.sys->CSPR, 3),
                  cpp_box::arm::test_bit(status.sys->CSPR, 2),
                  cpp_box::arm::test_bit(status.sys->CSPR, 1),
                  cpp_box::arm::test_bit(status.sys->CSPR, 0));

      if (ImGui::CollapsingHeader("Memory")) {
        const auto sp                   = status.sys->SP();
        const std::uint32_t stack_start = sp > STACK_START - 5 * 4 ? STACK_START : sp;
        const auto pc                   = status.sys->PC() - 4;
        const std::uint32_t pc_start    = pc < 5 * 4 ? 0 : pc - 5 * 4;

        ImGui::Text("Stack Pointer (SP)     Next Instruction (PC-4)");

        for (std::uint32_t idx = 0; idx < 44; idx += 4) {
          const auto sp_loc = stack_start - idx;
          if (sp_loc == sp) {
            ImGui::Text("%08x: %08x    ", sp_loc, status.sys->read_word(sp_loc));
          } else {
            ImGui::TextDisabled("%08x: %08x    ", sp_loc, status.sys->read_word(sp_loc));
          }
          ImGui::SameLine();
          const auto pc_loc = pc_start + idx;
          const auto word   = status.sys->read_word(pc_loc);
          if (pc_loc == pc) {
            ImGui::Text("%08x: %08x %s", pc_loc, word, status.loaded_files.disassembly[word].c_str());
          } else {
            ImGui::TextDisabled("%08x: %08x %s", pc_loc, word, status.loaded_files.disassembly[word].c_str());
          }
        }
      }
    }
    ImGui::End();


    ImGui::Begin("C++");
    {
      if (status.loaded_files.src.size() - strlen(status.loaded_files.src.c_str()) < 256) {
        status.loaded_files.src.resize(status.loaded_files.src.size() + 512);
      }
      ImGui::Checkbox("Show Assembly", &status.show_assembly);
      const auto available = ImGui::GetContentRegionAvail();
      ImGui::BeginChild("Code", { status.show_assembly ? (available.x * 5 / 8) : available.x, available.y });
      {
        const auto source_changed =
          ImGui::InputTextMultiline("", status.loaded_files.src.data(), status.loaded_files.src.size(), ImGui::GetContentRegionAvail());
        status.needs_build = status.needs_build || source_changed;
      }
      ImGui::EndChild();
      ImGui::SameLine();
      if (status.show_assembly) {
        ImGui::BeginChild("Assembly", ImGui::GetContentRegionAvail());
        {
          ImGui::InputTextMultiline("",
                                    status.loaded_files.assembly.data(),
                                    status.loaded_files.assembly.size(),
                                    ImGui::GetContentRegionAvail(),
                                    ImGuiInputTextFlags_ReadOnly);
        }
        ImGui::EndChild();
      }
    }
    ImGui::End();

    ImGui::Begin("Goals", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
    {
      auto current_goal = static_cast<int>(status.current_goal);
      ImGui::SliderInt("Current Goal", &current_goal, 0, static_cast<int>(status.goals.size() - 1));
      status.current_goal = current_goal;
      auto &goal          = status.goals.at(status.current_goal);
      ImGui::Separator();
      bool completed = goal.completed;
      ImGui::Checkbox("", &completed);
      ImGui::SameLine();
      ImGui::Text(goal.name.c_str());
      ImGui::Text(goal.description.c_str());
      for (std::size_t clue = 0; clue < goal.hints.size(); ++clue) {
        if (ImGui::CollapsingHeader(fmt::format("Show Hint #{}", clue).c_str())) { ImGui::Text(goal.hints[clue].c_str()); }
      }
    }
    ImGui::End();

    return inputs;
  }

  struct Goal
  {
    std::string name;
    std::string description;
    std::vector<std::string> hints;
    std::function<bool(const Status &)> completion_state;
    std::size_t hints_shown{ 0 };
    bool completed{ false };
  };

  static std::vector<Goal> generate_goals()
  {
    return { { "Compile a Program",
               "Make a simple program with a `main` function that compiles\nand produces a binary and returns 0",
               { "a simple `main` in C++ has this signature: `int main();`",
                 "0 is returned by default",
                 "Your program should look something like: `int main() {}`" },
               [](const Status &s) -> bool { return s.sys->registers[0] == 0; },
               0,
               false },
             { "Return 5 From Main",
               "Make a simple program with a `main` function that compiles\nand produces a binary and returns 5",
               { "To make a function return a value, you use the `return` keyword",
                 "0 is returned by default",
                 "Your program should look something like: `int main() { return 5; }`" },
               [](const Status &s) -> bool { return s.sys->registers[0] == 5; },
               0,
               false } };
  }


  void event_loop(const std::filesystem::path &original_path)
  {
    std::uniform_int_distribution<std::uint16_t> distribution(0, 255);
    std::default_random_engine generator;


    console->set_level(spdlog::level::trace);
    console->set_pattern("[%Y-%m-%d %H:%M:%S %z] [%n] [%^%l%$] [thread %t] %v");
    console->info("C++ Box Starting");
    console->info("Original Path: {}", original_path.string());


    ImGui::CreateContext();
    sf::RenderWindow window(sf::VideoMode(1024, 768), "C++ Box");


    ImGui::SFML::Init(window);

    sf::Clock deltaClock;

    Status status{ *console, original_path, generate_goals() };
    window.setFramerateLimit(Status::FPS);

    while (window.isOpen()) {

      sf::Event event;
      while (window.pollEvent(event)) {
        ImGui::SFML::ProcessEvent(event);

        if (event.type == sf::Event::Closed) { window.close(); }
      }


      ImGui::SFML::Update(window, deltaClock.restart());

      switch (status.next_state(draw_interface(status))) {
      case Status::States::Running:
        for (int i = 0; i < status.opsPerFrame && status.sys->operations_remaining(); ++i) { status.sys->next_operation(); }
        status.update_display();
        break;
      case Status::States::Begin_Build:
        status.future_build = std::async(std::launch::async, [console = this->console, src = status.loaded_files.src, compiler = this->compiler]() {
        // string is oversized to allow for a buffer for IMGUI, need to only compile the first part of it
#if defined(_MSC_VER)
          return compile(src.substr(0, src.find('\0')), compiler, "3", "c++2a", *console);
#else
          return compile(src.substr(0, src.find('\0')), compiler, "3", "c++2a", *console);
#endif
        });
        status.needs_build = false;
        break;
      case Status::States::Parse_Build_Results:
        if (!status.needs_build) {
          status.loaded_files = status.future_build.get();
          console->info("Results Loaded");
        } else {
          status.future_build.get();
          console->info("Skipping results loading, build needed");
        }
        break;
      case Status::States::Paused: break;
      case Status::States::Reset:
        status.reset();
        status.update_display();
        break;
      case Status::States::Start: break;
      case Status::States::Reset_Timer: status.reset_static_timer(); break;
      case Status::States::Step_One:
        if (status.sys->operations_remaining()) {
          status.sys->next_operation();
          status.update_display();
        }
        break;
      case Status::States::Static: {
        const auto texture_size = status.texture.getSize();
        std::vector<std::uint8_t> data(texture_size.x * texture_size.y * 4);
        std::generate(data.begin(), data.end(), [&distribution, &generator]() { return static_cast<std::uint8_t>(distribution(generator)); });
        status.texture.update(data.data());
      } break;
      case Status::States::Check_Goal:
        if (status.current_goal <= status.goals.size() && status.goals[status.current_goal].completion_state(status)) {
          status.goals[status.current_goal].completed = true;
          status.current_goal                         = std::min(status.current_goal + 1, status.goals.size() - 1);
        }
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

  std::filesystem::path compiler{};

  explicit Box(std::filesystem::path t_compiler) : compiler(std::move(t_compiler)) {}
};


int main(const int argc, const char *argv[])
{
  using clara::Opt;
  using clara::Arg;
  using clara::Args;
  using clara::Help;
  bool showHelp{ false };
  std::filesystem::path initialFile;
#if defined(_MSC_VER)
  std::filesystem::path compiler("/usr/local/bin/clang++");
#else
  std::filesystem::path compiler(R"(C:\Program Files\LLVM\bin\clang++)");
#endif
  auto cli = Help(showHelp) | Opt(compiler, "path")["--compiler"]("compile C++ with <compiler>")
             | Arg(initialFile, "file")("load <file> as an initial program");

  auto result = cli.parse(Args(argc, argv));
  if (!result) {
    std::cerr << "Error in command line: " << result.errorMessage() << '\n';
    return 1;
  }

  if (showHelp) {
    std::cout << cli << '\n';
    return 0;
  }

  Box box(compiler);

  box.event_loop(initialFile);
}
