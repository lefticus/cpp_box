#include "../include/cpp_box/arm.hpp"
#include "../include/cpp_box/elf_reader.hpp"
#include "../include/cpp_box/state_machine.hpp"
#include "../include/cpp_box/memory_map.hpp"
#include "../include/cpp_box/utility.hpp"
#include "../include/cpp_box/compiler.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "imgui/lib/imgui-SFML.h"
#include "imgui/lib/imgui.h"

#include "../include/cpp_box/utility.hpp"

#include <SFML/Graphics/CircleShape.hpp>
#include <SFML/Graphics/RenderWindow.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/System/Clock.hpp>
#include <SFML/Window/Event.hpp>

#include <clara.hpp>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>


struct Box
{

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
    std::array<std::uint32_t, 16> last_registers{};
    std::uint32_t last_CSPR{};

    // TODO: move somewhere shared
    struct Timer
    {
      explicit Timer(const float timeoutInSeconds) noexcept : timeout{ timeoutInSeconds } {}
      bool expired() const noexcept { return timer.getElapsedTime().asSeconds() >= timeout; }
      void reset() noexcept { timer.restart(); }

    private:
      sf::Clock timer;
      float timeout;
    };


    cpp_box::Loaded_Files loaded_files;
    Timer static_timer{ 0.5f };

    bool build_good() const noexcept { return loaded_files.good_binary; }
    std::unique_ptr<cpp_box::arm::System<cpp_box::system::TOTAL_RAM, std::vector<std::uint8_t>>> sys;
    std::vector<Goal> goals;
    std::size_t current_goal{ 0 };

    sf::Texture texture;
    sf::Sprite sprite;

    static constexpr auto FPS               = 30;
    static constexpr const auto opsPerFrame = 30'000'000 / FPS;

    static constexpr auto s_build_ready       = [](const auto &status, const auto & /**/) { return status.build_ready(); };
    static constexpr auto s_running           = [](const auto &status, const auto & /**/) { return !status.paused && status.build_good(); };
    static constexpr auto s_paused            = [](const auto &status, const auto & /**/) { return status.paused && status.build_good(); };
    static constexpr auto s_failed            = [](const auto &status, const auto & /**/) { return !status.build_good(); };
    static constexpr auto s_static_timer      = [](const auto &status, const auto & /**/) { return !status.static_timer.expired(); };
    static constexpr auto s_can_start_build   = [](const auto &status, const auto & /**/) { return status.needs_build && !status.is_building(); };
    static constexpr auto s_always_true       = [](const auto & /**/, const auto & /**/) { return true; };
    static constexpr auto s_reset_pressed     = [](const auto & /**/, const auto &inputs) { return inputs.reset_pressed; };
    static constexpr auto s_step_pressed      = [](const auto & /**/, const auto &inputs) { return inputs.step_pressed; };
    static constexpr auto s_goal_check_needed = [](const auto &status, const auto & /**/) {
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
                                            cpp_box::state_machine::StateTransition{ States::Paused, States::Step_One, s_step_pressed },
                                            cpp_box::state_machine::StateTransition{ States::Paused, States::Begin_Build, s_can_start_build },
                                            cpp_box::state_machine::StateTransition{ States::Paused, States::Running, s_running },
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
      sys =
        std::make_unique<decltype(sys)::element_type>(loaded_files.image, static_cast<std::uint32_t>(cpp_box::system::Memory_Map::USER_RAM_START));
      sys->setup_run(static_cast<std::uint32_t>(loaded_files.entry_point)
                     + static_cast<std::uint32_t>(cpp_box::system::Memory_Map::USER_RAM_START));
      cpp_box::utility::runtime_assert(sys->SP() == cpp_box::system::STACK_START);
      m_logger.trace("setting up registers");
      sys->write_word(static_cast<std::uint32_t>(cpp_box::system::Memory_Map::RAM_SIZE), cpp_box::system::TOTAL_RAM);
      sys->write_half_word(static_cast<std::uint32_t>(cpp_box::system::Memory_Map::SCREEN_WIDTH), 64);
      sys->write_half_word(static_cast<std::uint32_t>(cpp_box::system::Memory_Map::SCREEN_HEIGHT), 64);
      sys->write_byte(static_cast<std::uint32_t>(cpp_box::system::Memory_Map::SCREEN_BPP), 32);
      sys->write_word(static_cast<std::uint32_t>(cpp_box::system::Memory_Map::SCREEN_BUFFER), cpp_box::system::DEFAULT_SCREEN_BUFFER);
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
      , loaded_files{ cpp_box::load_unknown(path, m_logger) }
      , sys{ std::make_unique<decltype(sys)::element_type>(loaded_files.image,
                                                           static_cast<std::uint32_t>(cpp_box::system::Memory_Map::USER_RAM_START)) }
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
      sf::Vector2u size{ sys->read_half_word(static_cast<std::uint32_t>(cpp_box::system::Memory_Map::SCREEN_WIDTH)),
                         sys->read_half_word(static_cast<std::uint32_t>(cpp_box::system::Memory_Map::SCREEN_HEIGHT)) };
      if (size != texture.getSize()) {
        m_logger.trace("Resizing screen to {}, {}", size.x, size.y);
        texture.create(size.x, size.y);
        sprite.setTexture(texture, true);
      }

      if (const auto display_loc = sys->read_word(static_cast<std::uint32_t>(cpp_box::system::Memory_Map::SCREEN_BUFFER));
          cpp_box::system::TOTAL_RAM - display_loc >= size.x * size.y * 4) {
        texture.update(&sys->builtin_ram[display_loc]);
      } else {
        // write as many lines as we can if we're past the end of RAM
        const auto pixels_to_write = std::min(size.x * size.y, (cpp_box::system::TOTAL_RAM - display_loc) / 4);
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

    std::future<cpp_box::Loaded_Files> future_build;
    bool needs_build{ true };


  private:
    void scale_impl(const float last_scale_factor)
    {
      ImGui::GetStyle().ScaleAllSizes(scale_factor / last_scale_factor);
      sprite.setScale(scale_factor * sprite_scale_factor, scale_factor * sprite_scale_factor);
      ImGui::GetIO().FontGlobalScale = scale_factor;
    };
  };

  template<typename StringType, typename... Params> void text(const bool enabled, const StringType &format_str, Params &&... params)
  {
    if (!enabled) { ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]); }
    const auto s     = fmt::format(static_cast<const char *>(format_str), std::forward<Params>(params)...);
    const auto begin = s.c_str();
    const auto end   = begin + s.size();  // NOLINT, this is save ptr arithmetic and std::next requires a signed type :P
    ImGui::TextUnformatted(begin, end);
    if (!enabled) { ImGui::PopStyleColor(); }
  }

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
      text(true, "{:2.2f} FPS ~{:2.2f} Mhz", 1 / elapsedSeconds, status.opsPerFrame / elapsedSeconds / 1000000);

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
      if (ImGui::CollapsingHeader("Registers")) {
        for (std::size_t i = 0; i < 16; ++i) {
          switch (i) {
          case 13: text(true, "SP "); break;
          case 14: text(true, "LR "); break;
          case 15: text(true, "PC "); break;
          default: text(true, "R{:<2}", i);
          }
          ImGui::SameLine();
          text(status.sys->registers[i] != status.last_registers[i], "{:08x}", status.sys->registers[i]);
          if (i != 7 && i != 15) { ImGui::SameLine(); }
        }


        text(true, "     NZCV                    IFT     ");
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, { 2, 0 });
        text(true, "CSPR ");
        for (std::size_t bit = 0; bit < 32; ++bit) {
          ImGui::SameLine();
          const auto new_bit = cpp_box::arm::test_bit(status.sys->CSPR, 31 - bit);
          const auto old_bit = cpp_box::arm::test_bit(status.last_CSPR, 31 - bit);
          text(new_bit != old_bit, "{:d}", cpp_box::arm::test_bit(status.sys->CSPR, 31 - bit));
        }
        ImGui::PopStyleVar();
      }

      if (ImGui::CollapsingHeader("Memory")) {
        const auto sp                   = status.sys->SP();
        const std::uint32_t stack_start = sp > cpp_box::system::STACK_START - 5 * 4 ? cpp_box::system::STACK_START : sp;
        const auto pc                   = status.sys->PC() - 4;
        const std::uint32_t pc_start    = pc < 5 * 4 ? 0 : pc - 5 * 4;

        text(true, "Stack Pointer (SP)     Next Instruction (PC-4)");

        for (std::uint32_t idx = 0; idx < 44; idx += 4) {
          const auto sp_loc = stack_start - idx;
          text(sp_loc != sp, "{:08x}: {:08x}    ", sp_loc, status.sys->read_word(sp_loc));
          ImGui::SameLine();
          const auto pc_loc = pc_start + idx;
          const auto word   = status.sys->read_word(pc_loc);
          text(pc_loc == pc,
               "{:08x}: {:08x} {}",
               pc_loc,
               word,
               status.loaded_files.location_data[pc_loc - static_cast<std::uint32_t>(cpp_box::system::Memory_Map::USER_RAM_START)]
                 .disassembly.c_str());
        }
      }


      if (ImGui::CollapsingHeader("Source")) {
        const auto pc              = status.sys->PC() - 4;
        const auto object_loc      = pc - static_cast<uint32_t>(cpp_box::system::Memory_Map::USER_RAM_START);
        const auto current_linenum = status.loaded_files.location_data[object_loc].line_number;
        ImGui::BeginChild("Active Source", { ImGui::GetContentRegionAvailWidth(), 300 });
        std::size_t endl  = 0;
        std::size_t begin = 0;
        int linenum       = 1;
        while (endl != std::string::npos) {
          begin                   = std::exchange(endl, status.loaded_files.src.find('\n', endl));
          const auto line         = status.loaded_files.src.substr(begin, endl - begin);
          const auto current_line = linenum == current_linenum;
          text(current_line, "{:4}: {}", linenum, line);
          if (current_line) { ImGui::SetScrollHere(); }
          if (endl != std::string::npos) { ++endl; }
          ++linenum;
        }
        ImGui::EndChild();
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
      status.current_goal = static_cast<decltype(status.current_goal)>(current_goal);
      auto &goal          = status.goals.at(status.current_goal);
      ImGui::Separator();
      bool completed = goal.completed;
      ImGui::Checkbox("", &completed);
      ImGui::SameLine();
      text(true, "{}", goal.name);
      text(true, "{}", goal.description);
      for (std::size_t clue = 0; clue < goal.hints.size(); ++clue) {
        if (ImGui::CollapsingHeader(fmt::format("Show Hint #{}", clue).c_str())) { text(true, "{}", goal.hints[clue]); }
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
    std::random_device r;
    std::default_random_engine generator{ r() };


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

      sf::Event event{};
      while (window.pollEvent(event)) {
        ImGui::SFML::ProcessEvent(event);

        if (event.type == sf::Event::Closed) { window.close(); }
      }


      ImGui::SFML::Update(window, deltaClock.restart());

      switch (status.next_state(draw_interface(status))) {
      case Status::States::Running:
        status.last_registers = status.sys->registers;
        status.last_CSPR      = status.sys->CSPR;
        for (int i = 0; i < status.opsPerFrame && status.sys->operations_remaining(); ++i) { status.sys->next_operation(); }
        status.update_display();
        break;
      case Status::States::Begin_Build:
        status.future_build = std::async(
          std::launch::async,
          [console             = this->console,
           src                 = status.loaded_files.src,
           clang_compiler      = this->clang_compiler,
           freestanding_stdlib = this->freestanding_stdlib,
           hardware_lib        = this->hardware_lib]() {
            // string is oversized to allow for a buffer for IMGUI, need to only compile the first part of it
            return cpp_box::compile(src.substr(0, src.find('\0')), clang_compiler, freestanding_stdlib, hardware_lib, "3", "c++2a", *console);
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
          status.last_registers = status.sys->registers;
          status.last_CSPR      = status.sys->CSPR;
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

  std::filesystem::path clang_compiler{};
  std::filesystem::path freestanding_stdlib{};
  std::filesystem::path hardware_lib{};

  Box(std::filesystem::path t_clang_compiler, std::filesystem::path t_freestanding_stdlib, std::filesystem::path t_hardware_lib)
    : clang_compiler{ std::move(t_clang_compiler) }
    , freestanding_stdlib{ std::move(t_freestanding_stdlib) }
    , hardware_lib{ std::move(t_hardware_lib) }
  {
  }
};


int main(const int argc, const char *argv[])
{
  using clara::Opt;
  using clara::Arg;
  using clara::Args;
  using clara::Help;
  bool showHelp{ false };
  std::filesystem::path initialFile;

  std::filesystem::path user_provided_clang;
  std::filesystem::path user_provided_freestanding_stdlib;
  std::filesystem::path user_provided_hardware_lib;

  auto cli = Help(showHelp) | Opt(user_provided_clang, "path")["--clang_compiler"]("compile C++ with <clang_compiler>")
             | Opt(user_provided_freestanding_stdlib, "path")["--freestanding_stdlib"]("freestanding stdlib implementation to use")
             | Opt(user_provided_hardware_lib, "path")["--hardware_lib"]("hardware lib implementation to use")
             | Arg(initialFile, "file")("load <file> as an initial program");

  auto result = cli.parse(Args(argc, argv));
  if (!result) {
    std::cerr << "Error in command line: " << result.errorMessage() << '\n';
    return EXIT_FAILURE;
  }

  if (showHelp) {
    std::cout << cli << '\n';
    return EXIT_SUCCESS;
  }

  const auto clang_compiler = cpp_box::find_clang(user_provided_clang, R"(C:\Program Files\LLVM\bin\clang++)", "/usr/local/bin/clang++", "/usr/bin/clang++");

  if (clang_compiler.empty()) {
    std::cerr << "Unable to locate a viable clang compiler\n";
    return EXIT_FAILURE;
  } else {
    std::cout << "Using compiler: '" << clang_compiler << "'\n";
  }

  Box box(clang_compiler, user_provided_freestanding_stdlib, user_provided_hardware_lib);

  box.event_loop(initialFile);
}
