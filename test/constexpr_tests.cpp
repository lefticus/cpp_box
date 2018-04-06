#define CATCH_CONFIG_MAIN  // This tells Catch to provide a main()
#include "catch.hpp"

#include <arm_thing/arm.hpp>

template<bool B> bool static_test()
{
  static_assert(B);
  return B;
}

template<typename... T> constexpr auto run_instruction(T... instruction)
{
  ARM_Thing::System system{};
  (system.process(instruction), ...);
  return system;
}

template<std::size_t N> constexpr auto run_code(std::uint32_t start, std::array<std::uint8_t, N> memory)
{
  ARM_Thing::System system{ memory };
  system.run(start);
  return system;
}


TEST_CASE("test always executing jump")
{
  constexpr auto systest2 = run_instruction(ARM_Thing::Instruction{ 0b1110'1010'0000'0000'0000'0000'0000'1111 });
  REQUIRE(static_test<systest2.PC() == 68>());
  REQUIRE(static_test<systest2.registers[14] == 0>());
}

TEST_CASE("test always executing jump with saved return")
{
  constexpr auto systest3 = run_instruction(ARM_Thing::Instruction{ 0b1110'1011'0000'0000'0000'0000'0000'1111 });
  REQUIRE(static_test<systest3.PC() == 68>());
  REQUIRE(static_test<systest3.registers[14] == 4>());
}

TEST_CASE("test add of register")
{
  constexpr auto systest4 = run_instruction(ARM_Thing::Instruction{ 0xe2800055 });  // add r0, r0, #85
  REQUIRE(static_test<systest4.registers[0] == 0x55>());
}

TEST_CASE("test add of register with shifts")
{
  constexpr auto systest5 = run_instruction(ARM_Thing::Instruction{ 0xe2800055 },  // add r0, r0, #85
                                            ARM_Thing::Instruction{ 0xe2800c7e }   // add r0, r0, #32256
  );
  REQUIRE(static_test<systest5.registers[0] == (85 + 32256)>());
}

TEST_CASE("test multiple adds and sub")
{
  constexpr auto systest6 = run_instruction(ARM_Thing::Instruction{ 0xe2800001 },  // add r0, r0, #1
                                            ARM_Thing::Instruction{ 0xe2811009 },  // add r1, r1, #9
                                            ARM_Thing::Instruction{ 0xe2822002 },  // add r2, r2, #2
                                            ARM_Thing::Instruction{ 0xe0423001 }   // sub r3, r2, r1
  );
  REQUIRE(static_test<systest6.registers[3] == static_cast<std::uint32_t>(2 - 9)>());
}


TEST_CASE("test memory writes")
{
  auto systest6 = run_instruction(ARM_Thing::Instruction{ 0xe3a00064 },  // mov r0, #100 ; 0x64
                                            ARM_Thing::Instruction{ 0xe3a01005 },  // mov r1, #5
                                            ARM_Thing::Instruction{ 0xe5c01000 },  // strb r1, [r0]
                                            ARM_Thing::Instruction{ 0xe3a00000 },  // mov r0, #0
                                            ARM_Thing::Instruction{ 0xe1a0f00e }   // mov pc, lr
  );

  //REQUIRE(static_test<systest6.read_byte(100) == 5>());
}


TEST_CASE("test lsr")
{
  constexpr auto sys = run_instruction(ARM_Thing::Instruction{ 0xe3a03005 },  // mov r3, #5
                                       ARM_Thing::Instruction{ 0xe1a02123 }   // lsr r2, r3, #2
  );
  REQUIRE(static_test<sys.registers[2] == 1>());
  REQUIRE(static_test<sys.registers[3] == 5>());
}

TEST_CASE("Test sub instruction with shift")
{
  constexpr auto systest7 = run_instruction(ARM_Thing::Instruction{ 0xe2800001 },  // add r0, r0, #1
                                            ARM_Thing::Instruction{ 0xe2811009 },  // add r1, r1, #9
                                            ARM_Thing::Instruction{ 0xe2822002 },  // add r2, r2, #2
                                            ARM_Thing::Instruction{ 0xe0403231 }   // sub r3, r0, r1, lsr r2
                                                                                   // logical right shift r1 by the number in bottom byte of r2
                                                                                   // subtract result from r0 and put answer in r3
  );

  REQUIRE(static_test<systest7.registers[3] == static_cast<std::uint32_t>(1 - (9 >> 2))>());
}

TEST_CASE("Test arbitrary code execution with loop")
{
  //#include <cstdint>

  // template<typename T>
  //  volatile T& get_loc(const std::uint32_t loc)
  //  {
  //    return *reinterpret_cast<T*>(loc);
  //  }

  // int main()
  //{
  //  for (int i = 0; i < 100; ++i) {
  //    get_loc<std::int8_t>(100+i) = i%5;
  //  }
  //}

  // compiles to:

  // 00000000 <main>:
  // 0:  e59f102c ldr	r1, [pc, #44]	; 34 <main+0x34>
  // 4:  e3a00000 mov	r0, #0
  // 8:  e0832190 umull	r2, r3, r0, r1
  // c:  e1a02123 lsr	r2, r3, #2
  // 10: e0822102 add	r2, r2, r2, lsl #2
  // 14: e2622000 rsb	r2, r2, #0
  // 18: e0802002 add	r2, r0, r2
  // 1c: e5c02064 strb	r2, [r0, #100]	; 0x64
  // 20: e2800001 add	r0, r0, #1
  // 24: e3500064 cmp	r0, #100	; 0x64
  // 28: 1afffff6 bne	8 <main+0x8>
  // 2c: e3a00000 mov	r0, #0
  // 30: e1a0f00e mov	pc, lr
  // 34: cccccccd .word	0xcccccccd

  constexpr std::array<std::uint8_t, 1024> memory{ 0x2c, 0x10, 0x9f, 0xe5, 0x00, 0x00, 0xa0, 0xe3, 0x90, 0x21, 0x83, 0xe0, 0x23, 0x21,
                                                   0xa0, 0xe1, 0x02, 0x21, 0x82, 0xe0, 0x00, 0x20, 0x62, 0xe2, 0x02, 0x20, 0x80, 0xe0,
                                                   0x64, 0x20, 0xc0, 0xe5, 0x01, 0x00, 0x80, 0xe2, 0x64, 0x00, 0x50, 0xe3, 0xf6, 0xff,
                                                   0xff, 0x1a, 0x00, 0x00, 0xa0, 0xe3, 0x0e, 0xf0, 0xa0, 0xe1, 0xcd, 0xcc, 0xcc, 0xcc };

  constexpr auto system = run_code(0, memory);

  REQUIRE(static_test<system.read_byte(100) == 0>());
  REQUIRE(static_test<system.read_byte(104) == 4>());
  REQUIRE(static_test<system.read_byte(105) == 0>());
  REQUIRE(static_test<system.read_byte(106) == 1>());
}


void test_condition_parsing()
{
  REQUIRE(static_test<ARM_Thing::Instruction{ 0b1110'1010'0000'0000'0000'0000'0000'1111 }.get_condition() == ARM_Thing::Condition::AL>());
}


