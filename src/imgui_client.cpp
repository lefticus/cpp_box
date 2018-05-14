#include "arm_thing/arm.hpp"
#include <string>
#include <vector>
#include <fstream>
#include <iostream>
#include <iomanip>

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

    ARM_Thing::System<65536> sys{ RAM };
    dump_rom(RAM);

    constexpr auto FPS = 30;

    ImGui::CreateContext();
    sf::RenderWindow window(sf::VideoMode(640, 480), "ImGui + SFML = <3");
    window.setFramerateLimit(FPS);
    ImGui::SFML::Init(window);

    sf::Texture texture;
    if (!texture.create(100, 100)) return -1;
    // Create a sprite that will display the texture
    sf::Sprite sprite(texture);
    sprite.setScale(3.0, 3.0);
    sf::Clock framerateClock;
    sf::Clock deltaClock;
    constexpr const auto opsPerFrame = 5000000/FPS;

    bool paused = true;
    bool step_one = false;

    std::string src;
    src.resize(1024);

    std::string bin;
    bin.resize(1024);


    while (window.isOpen()) {
      sf::Event event;
      while (window.pollEvent(event)) {
        ImGui::SFML::ProcessEvent(event);

        if (event.type == sf::Event::Closed) { window.close(); }
      }

      if (!paused) {
        for (int i = 0; i < opsPerFrame; ++i) {
          sys.next_operation();
        }
      } else if (step_one) {
        sys.next_operation();
        step_one = false;
      }

      texture.update(&sys.builtin_ram[0x4000]);


      ImGui::SFML::Update(window, deltaClock.restart());

      ImGui::Begin("Stats");
      const auto elapsedSeconds = framerateClock.restart().asSeconds();
      ImGui::Text("%02.2f FPS ~%02.2f Mhz", 1/elapsedSeconds, opsPerFrame/elapsedSeconds/1000000);
      ImGui::End();

      ImGui::Begin("Screen");
      ImGui::Image(sprite);
      ImGui::End();

      ImGui::Begin("Registers");
      ImGui::Text("R0 %04x R1 %04x R2  %04x R3  %04x R4  %04x R5 %04x R6 %04x R7 %04x",
                  sys.registers[0],
                  sys.registers[1],
                  sys.registers[2],
                  sys.registers[3],
                  sys.registers[4],
                  sys.registers[5],
                  sys.registers[6],
                  sys.registers[7]);

      ImGui::Text("R8 %04x R9 %04x R10 %04x R11 %04x R12 %04x SP %04x LR %04x PC %04x",
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
          ARM_Thing::test_bit(sys.CSPR, 31),
          ARM_Thing::test_bit(sys.CSPR, 30),
          ARM_Thing::test_bit(sys.CSPR, 29),
          ARM_Thing::test_bit(sys.CSPR, 28),
          ARM_Thing::test_bit(sys.CSPR, 27),
          ARM_Thing::test_bit(sys.CSPR, 26),
          ARM_Thing::test_bit(sys.CSPR, 25),
          ARM_Thing::test_bit(sys.CSPR, 24),
          ARM_Thing::test_bit(sys.CSPR, 23),
          ARM_Thing::test_bit(sys.CSPR, 22),
          ARM_Thing::test_bit(sys.CSPR, 21),
          ARM_Thing::test_bit(sys.CSPR, 20),
          ARM_Thing::test_bit(sys.CSPR, 19),
          ARM_Thing::test_bit(sys.CSPR, 18),
          ARM_Thing::test_bit(sys.CSPR, 17),
          ARM_Thing::test_bit(sys.CSPR, 16),
          ARM_Thing::test_bit(sys.CSPR, 15),
          ARM_Thing::test_bit(sys.CSPR, 14),
          ARM_Thing::test_bit(sys.CSPR, 13),
          ARM_Thing::test_bit(sys.CSPR, 12),
          ARM_Thing::test_bit(sys.CSPR, 11),
          ARM_Thing::test_bit(sys.CSPR, 10),
          ARM_Thing::test_bit(sys.CSPR, 9),
          ARM_Thing::test_bit(sys.CSPR, 8),
          ARM_Thing::test_bit(sys.CSPR, 7),
          ARM_Thing::test_bit(sys.CSPR, 6),
          ARM_Thing::test_bit(sys.CSPR, 5),
          ARM_Thing::test_bit(sys.CSPR, 4),
          ARM_Thing::test_bit(sys.CSPR, 3),
          ARM_Thing::test_bit(sys.CSPR, 2),
          ARM_Thing::test_bit(sys.CSPR, 1),
          ARM_Thing::test_bit(sys.CSPR, 0)
          );
      ImGui::End();

      ImGui::Begin("Controls");
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

      if (ImGui::Button("Reset")) {
        sys = decltype(sys){RAM};
      }
      ImGui::End();

      ImGui::Begin("Code");
      if (ImGui::InputTextMultiline("Src", src.data(), src.size())) {
        if (std::ofstream ofs("/tmp/src.cpp"); ofs.good()) {
          ofs.write(src.data(), src.find_first_of('\0', 0));
          ofs.flush();
        }
        system("/home/jason/clang-trunk/bin/clang++ /tmp/src.cpp -S -o /tmp/src.asm -O3 -mllvm --x86-asm-syntax=intel --target=armv4");
        if (std::ifstream ifs{ "/tmp/src.asm", std::ios::binary }; ifs.good()) {
          const auto file_size = ifs.seekg(0, std::ios_base::end).tellg();
          ifs.seekg(0);
          bin.resize(static_cast<std::size_t>(file_size));
          ifs.read(bin.data(), file_size);
        }
      }
      ImGui::SameLine();
      ImGui::InputTextMultiline("Bin", bin.data(), bin.size(), {}, ImGuiInputTextFlags_ReadOnly);
      ImGui::End();

      window.clear();
      ImGui::SFML::Render(window);
      window.display();
    }

    ImGui::SFML::Shutdown();
    ImGui::DestroyContext();
  }
}
