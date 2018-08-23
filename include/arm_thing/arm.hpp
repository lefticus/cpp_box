#include <array>
#include <tuple>
#include <algorithm>
#include <iterator>
#include <variant>

namespace arm_thing {

// necessary to deal with poor performing visit implementations from the std libs
template<std::size_t Idx, typename F, typename V> constexpr decltype(auto) simple_visit_impl(F &&f, V &&t)
{
  if constexpr (Idx == std::variant_size_v<V> - 1) {
    // if we get to the last item, stop recursion and assume that this
    // version is the matching one. If it is not, then the bad_variant_access
    // will automatically get thrown and propagated back out.
    return (f(std::get<Idx>(std::forward<V>(t))));
  } else {
    if (Idx == t.index()) {
      return (f(std::get<Idx>(std::forward<V>(t))));
    } else {
      return (simple_visit_impl<Idx + 1>(std::forward<F>(f), std::move(t)));
    }
  }
}

// Variation of the naive way: only loops as many times as there are set bits.
template<typename T>[[nodiscard]] constexpr T popcnt(T v) noexcept
{
  T c{ 0 };
  for (; v; ++c) { v &= v - 1; }
  return c;
}


template<typename F, typename V> constexpr decltype(auto) simple_visit(F &&f, V &&t)
{
  return (simple_visit_impl<0>(std::forward<F>(f), std::forward<V>(t)));
}

template<typename Value, typename Bit>[[nodiscard]] constexpr bool test_bit(const Value val, const Bit bit) noexcept
{
  return val & (static_cast<Value>(1) << bit);
}

template<typename Type, typename CRTP> struct Strongly_Typed
{
  [[nodiscard]] constexpr auto data() const noexcept { return m_val; }
  [[nodiscard]] constexpr auto operator&(const Type rhs) const noexcept { return m_val & rhs; }
  [[nodiscard]] constexpr bool test_bit(const Type bit) const noexcept { return arm_thing::test_bit(m_val, bit); }

  [[nodiscard]] friend constexpr auto operator&(const Type lhs, const CRTP rhs) noexcept { return lhs & rhs.m_val; }


  constexpr explicit Strongly_Typed(const Type val) noexcept : m_val(val) {}

protected:
  std::uint32_t m_val;
};

enum class Condition {
  EQ = 0b0000,  // Z set (equal)
  NE = 0b0001,  // Z clear (not equal)
  HS = 0b0010,  // C set (unsigned higher or same)
  CS = 0b0010,
  LO = 0b0011,  // C clear (unsigned lower)
  CC = 0b0011,
  MI = 0b0100,  // N set (negative)
  PL = 0b0101,  // N clear (positive or zero)
  VS = 0b0110,  // V set (overflow)
  VC = 0b0111,  // V clear (no overflow)
  HI = 0b1000,  // C set and Z clear (unsigned higher)
  LS = 0b1001,  // C clear or Z (set unsigned lower or same)
  GE = 0b1010,  // N set and V set, or N clear and V clear (> or =)
  LT = 0b1011,  // N set and V clear, or N clear and V set (>)
  GT = 0b1100,  // Z clear, and either N set and V set, or N clear and V set (>)
  LE = 0b1101,  // Z set, or N set and V clear,or N clear and V set (< or =)
  AL = 0b1110,  // Always
  NV = 0b1111   // Reserved
};

enum class OpCode {
  AND = 0b0000,  // Rd:= Op1 AND Op2
  EOR = 0b0001,  // Rd:= Op1 EOR Op2
  SUB = 0b0010,  // Rd:= Op1 - Op2
  RSB = 0b0011,  // Rd:= Op2 - Op1
  ADD = 0b0100,  // Rd:= Op1 + Op2
  ADC = 0b0101,  // Rd:= Op1 + Op2 + C
  SBC = 0b0110,  // Rd:= Op1 - Op2 + C
  RSC = 0b0111,  // Rd:= Op2 - Op1 + C
  TST = 0b1000,  // set condition codes on Op1 AND Op2
  TEQ = 0b1001,  // set condition codes on Op1 EOR Op2
  CMP = 0b1010,  // set condition codes on Op1 - Op2
  CMN = 0b1011,  // set condition codes on Op1 + Op2
  ORR = 0b1100,  // Rd:= Op1 OR Op2
  MOV = 0b1101,  // Rd:= Op2
  BIC = 0b1110,  // Rd:= Op1 AND NOT Op2
  MVN = 0b1111   // Rd:= NOT Op2
};

enum class Shift_Type { Logical_Left = 0b00, Logical_Right = 0b01, Arithmetic_Right = 0b10, Rotate_Right = 0b11 };


struct Instruction : Strongly_Typed<std::uint32_t, Instruction>
{
  using Strongly_Typed::Strongly_Typed;

  [[nodiscard]] constexpr Condition get_condition() const noexcept { return static_cast<Condition>(0b1111 & (m_val >> 28)); }
  [[nodiscard]] constexpr bool unconditional() const noexcept { return ((m_val >> 28) & 0b1111) == 0b1110; }

  friend struct Data_Processing;
  friend struct Single_Data_Transfer;
  friend struct Multiply_Long;
  friend struct Branch;
  friend struct Load_And_Store_Multiple;
};

struct Single_Data_Transfer : Strongly_Typed<std::uint32_t, Single_Data_Transfer>
{
  [[nodiscard]] constexpr bool immediate_offset() const noexcept { return !test_bit(25); }
  [[nodiscard]] constexpr bool pre_indexing() const noexcept { return test_bit(24); }
  [[nodiscard]] constexpr bool up_indexing() const noexcept { return test_bit(23); }
  [[nodiscard]] constexpr bool byte_transfer() const noexcept { return test_bit(22); }
  [[nodiscard]] constexpr bool write_back() const noexcept { return test_bit(21); }
  [[nodiscard]] constexpr bool load() const noexcept { return test_bit(20); }

  [[nodiscard]] constexpr auto base_register() const noexcept { return (m_val >> 16) & 0b1111; }
  [[nodiscard]] constexpr auto src_dest_register() const noexcept { return (m_val >> 12) & 0b1111; }
  [[nodiscard]] constexpr auto offset() const noexcept { return m_val & 0xFFF; }
  [[nodiscard]] constexpr auto offset_register() const noexcept { return offset() & 0b1111; }
  [[nodiscard]] constexpr auto offset_shift() const noexcept { return offset() >> 4; }
  [[nodiscard]] constexpr auto offset_shift_type() const noexcept { return static_cast<Shift_Type>((offset_shift() >> 1) & 0b11); }
  [[nodiscard]] constexpr auto offset_shift_amount() const noexcept { return offset_shift() >> 4; }

  constexpr explicit Single_Data_Transfer(Instruction ins) noexcept : Strongly_Typed{ ins.m_val } {}
};

struct Load_And_Store_Multiple : Strongly_Typed<std::uint32_t, Load_And_Store_Multiple>
{
  [[nodiscard]] constexpr bool pre_indexing() const noexcept { return test_bit(24); }
  [[nodiscard]] constexpr bool up_indexing() const noexcept { return test_bit(23); }
  [[nodiscard]] constexpr bool psr() const noexcept { return test_bit(22); }
  [[nodiscard]] constexpr bool write_back() const noexcept { return test_bit(21); }
  [[nodiscard]] constexpr bool load() const noexcept { return test_bit(20); }

  [[nodiscard]] constexpr auto base_register() const noexcept { return (m_val >> 16) & 0b1111; }
  [[nodiscard]] constexpr auto register_list() const noexcept -> std::uint16_t { return m_val & 0xFFFF; }

  constexpr explicit Load_And_Store_Multiple(Instruction ins) noexcept : Strongly_Typed{ ins.m_val } {}
};

struct Multiply_Long : Strongly_Typed<std::uint32_t, Multiply_Long>
{
  [[nodiscard]] constexpr bool unsigned_mul() const noexcept { return test_bit(22); }
  [[nodiscard]] constexpr bool accumulate() const noexcept { return test_bit(21); }
  [[nodiscard]] constexpr bool status_register_update() const noexcept { return test_bit(20); }
  [[nodiscard]] constexpr auto high_result() const noexcept { return (m_val >> 16) & 0b1111; }
  [[nodiscard]] constexpr auto low_result() const noexcept { return (m_val >> 12) & 0b1111; }
  [[nodiscard]] constexpr auto operand_1() const noexcept { return (m_val >> 8) & 0b1111; }
  [[nodiscard]] constexpr auto operand_2() const noexcept { return m_val & 0b1111; }

  constexpr explicit Multiply_Long(Instruction ins) noexcept : Strongly_Typed{ ins.m_val } {}
};

struct Branch : Strongly_Typed<std::uint32_t, Branch>
{
  [[nodiscard]] constexpr auto offset() const noexcept -> std::int32_t
  {
    if (test_bit(23)) {
      // is signed
      const auto twos_complement = ((~(m_val & 0x00FFFFFFF)) + 1) & 0x00FFFFFF;
      return -static_cast<int32_t>(twos_complement << 2);
    } else {
      return static_cast<int32_t>((m_val & 0x00FFFFFF) << 2);
    }
  };

  [[nodiscard]] constexpr bool link() const noexcept { return test_bit(24); }


  constexpr explicit Branch(Instruction ins) noexcept : Strongly_Typed{ ins.m_val } {}
};


struct Data_Processing : Strongly_Typed<std::uint32_t, Data_Processing>
{
  [[nodiscard]] constexpr OpCode get_opcode() const noexcept { return static_cast<OpCode>(0b1111 & (m_val >> 21)); }

  [[nodiscard]] constexpr auto operand_2() const noexcept { return 0b1111'1111'1111 & m_val; }
  [[nodiscard]] constexpr auto operand_2_register() const noexcept { return 0b1111 & m_val; }
  [[nodiscard]] constexpr bool operand_2_immediate_shift() const noexcept { return !test_bit(4); }
  [[nodiscard]] constexpr auto operand_2_shift_register() const noexcept { return operand_2() >> 8; }
  [[nodiscard]] constexpr auto operand_2_shift_amount() const noexcept { return operand_2() >> 7; }
  [[nodiscard]] constexpr auto operand_2_shift_type() const noexcept { return static_cast<Shift_Type>((operand_2() >> 5) & 0b11); }

  [[nodiscard]] constexpr auto operand_2_immediate() const noexcept
  {
    const auto op_2           = operand_2();
    const std::uint32_t value = 0b1111'1111 & op_2;

    const auto shift_right = (op_2 >> 8) * 2;

    // Avoiding UB for shifts >= size
    if (shift_right == 0 || shift_right == 32) { return static_cast<std::uint32_t>(value); }

    // Should create a ROR on any modern compiler.
    // no branching, CMOV on GCC
    return (value >> shift_right) | (value << (32 - shift_right));
  }

  [[nodiscard]] constexpr auto destination_register() const noexcept { return 0b1111 & (m_val >> 12); }

  [[nodiscard]] constexpr auto operand_1_register() const noexcept { return 0b1111 & (m_val >> 16); }
  [[nodiscard]] constexpr bool set_condition_code() const noexcept { return test_bit(20); }
  [[nodiscard]] constexpr bool immediate_operand() const noexcept { return test_bit(25); }


  constexpr explicit Data_Processing(Instruction ins) noexcept : Strongly_Typed{ ins.m_val } {}
};


enum class Instruction_Type {
  Data_Processing,
  MRS,
  MSR,
  MSRF,
  Multiply,
  Multiply_Long,
  Single_Data_Swap,
  Single_Data_Transfer,
  Undefined,
  Block_Data_Transfer,
  Branch,
  Coprocessor_Data_Transfer,
  Coprocessor_Data_Operation,
  Coprocessor_Register_Transfer,
  Software_Interrupt,
  Load_And_Store_Multiple
};


[[nodiscard]] constexpr auto get_lookup_table() noexcept
{
  // hack for lack of constexpr std::bitset
  constexpr const auto bitcount = [](const auto &tuple) {
    auto value = std::get<0>(tuple);
    int count  = 0;
    while (value) {  // until all bits are zero
      count++;
      value &= (value - 1);
    }
    return count;
  };

  // hack for lack of constexpr swap
  constexpr const auto swap_tuples = [](auto &lhs, auto &rhs) noexcept
  {
    auto old = lhs;

    std::get<0>(lhs) = std::get<0>(rhs);
    std::get<1>(lhs) = std::get<1>(rhs);
    std::get<2>(lhs) = std::get<2>(rhs);

    std::get<0>(rhs) = std::get<0>(old);
    std::get<1>(rhs) = std::get<1>(old);
    std::get<2>(rhs) = std::get<2>(old);
  };

  // ARMv3  http://netwinder.osuosl.org/pub/netwinder/docs/arm/ARM7500FEvB_3.pdf
  std::array<std::tuple<std::uint32_t, std::uint32_t, Instruction_Type>, 16> table{
    { { 0b0000'1100'0000'0000'0000'0000'0000'0000, 0b0000'0000'0000'0000'0000'0000'0000'0000, Instruction_Type::Data_Processing },
      { 0b0000'1111'1011'1111'0000'1111'1111'1111, 0b0000'0001'0000'1111'0000'1111'1111'1111, Instruction_Type::MRS },
      { 0b0000'1111'1011'1111'1111'1111'1111'0000, 0b0000'0001'0010'1001'1111'0000'0000'0000, Instruction_Type::MSR },
      { 0b0000'1101'1011'1111'1111'0000'0000'0000, 0b0000'0001'0010'1000'1111'0000'0000'0000, Instruction_Type::MSRF },
      { 0b0000'1111'1100'0000'0000'0000'1111'0000, 0b0000'0000'0000'0000'0000'0000'1001'0000, Instruction_Type::Multiply },
      { 0b0000'1111'1000'0000'0000'0000'1111'0000, 0b0000'0000'1000'0000'0000'0000'1001'0000, Instruction_Type::Multiply_Long },
      { 0b0000'1111'1011'0000'0000'1111'1111'0000, 0b0000'0001'0000'0000'0000'0000'1001'0000, Instruction_Type::Single_Data_Swap },
      { 0b0000'1100'0000'0000'0000'0000'0000'0000, 0b0000'0100'0000'0000'0000'0000'0000'0000, Instruction_Type::Single_Data_Transfer },
      { 0b0000'1110'0000'0000'0000'0000'0001'0000, 0b0000'0110'0000'0000'0000'0000'0001'0000, Instruction_Type::Undefined },
      { 0b0000'1110'0000'0000'0000'0000'0000'0000, 0b0000'1110'0000'0000'0000'0000'0000'0000, Instruction_Type::Block_Data_Transfer },
      { 0b0000'1110'0000'0000'0000'0000'0000'0000, 0b0000'1010'0000'0000'0000'0000'0000'0000, Instruction_Type::Branch },
      { 0b0000'1110'0000'0000'0000'0000'1111'0000, 0b0000'1100'0010'0000'0000'0000'0000'0000, Instruction_Type::Coprocessor_Data_Transfer },
      { 0b0000'1111'0000'0000'0000'0000'0001'0000, 0b0000'1110'0000'0000'0000'0000'0000'0000, Instruction_Type::Coprocessor_Data_Operation },
      { 0b0000'1111'0000'0000'0000'0000'0001'0000, 0b0000'1110'0000'0000'0000'0000'0001'0000, Instruction_Type::Coprocessor_Register_Transfer },
      { 0b0000'1111'0000'0000'0000'0000'0000'0000, 0b0000'1111'0000'0000'0000'0000'0000'0000, Instruction_Type::Software_Interrupt },
      { 0b0000'1110'0000'0000'0000'0000'0000'0000, 0b0000'1000'0000'0000'0000'0000'0000'0000, Instruction_Type::Load_And_Store_Multiple } },
  };

  // Order from most restrictive to least restrictive
  // Hack for lack of constexpr sort
  for (auto itr = begin(table); itr != end(table); ++itr) {
    swap_tuples(*itr, *std::min_element(itr, end(table), [bitcount](const auto &lhs, const auto &rhs) { return bitcount(lhs) > bitcount(rhs); }));
  }

  return table;
}


template<std::size_t RAM_Size = 1024> struct System
{
  std::uint32_t CSPR{};

  std::array<std::uint32_t, 16> registers{};
  bool invalid_memory_write{ false };

  [[nodiscard]] constexpr auto &SP() noexcept { return registers[13]; }
  [[nodiscard]] constexpr const auto &SP() const noexcept { return registers[13]; }

  [[nodiscard]] constexpr auto &LR() noexcept { return registers[14]; }
  [[nodiscard]] constexpr const auto &LR() const noexcept { return registers[14]; }

  [[nodiscard]] constexpr auto &PC() noexcept { return registers[15]; }
  [[nodiscard]] constexpr const auto &PC() const noexcept { return registers[15]; }

  struct ROM_Block
  {
    bool in_use{ false };
    std::uint8_t const *data{ nullptr };
    std::uint32_t start{ 0 };
    std::uint32_t end{ 0 };
  };

  struct RAM_Block
  {
    bool in_use{ false };
    std::uint8_t *data{ nullptr };
    std::uint32_t start{ 0 };
    std::uint32_t end{ 0 };
  };

  std::array<std::uint8_t, RAM_Size> builtin_ram{};

  constexpr void unhandled_instruction([[maybe_unused]] const Instruction ins, [[maybe_unused]] const Instruction_Type type) { abort(); }


  // read past end of allocated memory will return an unspecified value
  [[nodiscard]] constexpr std::uint8_t read_byte(const std::uint32_t loc) const noexcept
  {
    if (loc < RAM_Size) {
      return builtin_ram[loc];
    } else {
      return {};
    }
  }

  constexpr void write_byte(const std::uint32_t loc, const std::uint8_t value) noexcept
  {
    if (loc < RAM_Size) {
      builtin_ram[loc] = value;
    } else {
      invalid_memory_write = true;
    }
  }


  // read past end of allocated memory will return an unspecified value
  [[nodiscard]] constexpr std::uint32_t read_word(const std::uint32_t loc) const noexcept
  {
    const std::uint8_t *data = [&]() -> const std::uint8_t * {
      if (loc + 3 < RAM_Size) { return &builtin_ram[loc]; }
      return nullptr;
    }();

    if (data) {
      const std::uint32_t byte_1 = data[0];
      const std::uint32_t byte_2 = data[1];
      const std::uint32_t byte_3 = data[2];
      const std::uint32_t byte_4 = data[3];

      return byte_1 | (byte_2 << 8) | (byte_3 << 16) | (byte_4 << 24);
    } else {
      return {};
    }
  }

  constexpr void write_word(const std::uint32_t loc, const std::uint32_t value) noexcept
  {
    auto *data = [&]() -> std::uint8_t * {
      if (loc + 3 <= RAM_Size) { return &builtin_ram[loc]; }
      return nullptr;
    }();

    if (data) {
      data[0] = static_cast<std::uint8_t>(value & 0xFF);
      data[1] = static_cast<std::uint8_t>((value >> 8) & 0xFF);
      data[2] = static_cast<std::uint8_t>((value >> 16) & 0xFF);
      data[3] = static_cast<std::uint8_t>((value >> 24) & 0xFF);
    } else {
      invalid_memory_write = true;
    }
  }


  constexpr System() = default;

  template<typename Container> constexpr System(const Container &memory) noexcept
  {
    for (std::size_t loc = 0; loc < memory.size(); ++loc) {
      // cast is safe - we verified this statically assigned RAM was the right size
      write_byte(static_cast<std::uint32_t>(loc), memory[loc]);
    }
    i_cache.fill_cache(*this);
  }

  template<std::size_t Size> constexpr System(const std::array<std::uint8_t, Size> &memory) noexcept
  {
    static_assert(Size <= RAM_Size);

    for (std::size_t loc = 0; loc < Size; ++loc) {
      // cast is safe - we verified this statically assigned RAM was the right size
      write_byte(static_cast<std::uint32_t>(loc), memory[loc]);
    }

    i_cache.fill_cache(*this);
  }

  [[nodiscard]] constexpr auto get_instruction(const std::uint32_t PC) noexcept -> Instruction { return Instruction{ read_word(PC) }; }

  constexpr void setup_run(const std::uint32_t loc) noexcept
  {
    // Set return from call register, so when 'main' returns,
    // we'll be at the end of local RAM
    registers[14] = RAM_Size - 4;
    PC()          = loc + 4;
    SP()          = 0x8000;
  }

  template<typename Tracer = void (*)(const System &, std::uint32_t, Instruction)>
  constexpr void next_operation(Tracer &&tracer = [](const System &, const auto, const auto) {}) noexcept
  {
    const auto [ins, type] = i_cache.fetch(PC() - 4, *this);
    tracer(*this, PC() - 4, ins);
    process(ins, type);
  }

  [[nodiscard]] constexpr bool operations_remaining() const noexcept { return PC() != RAM_Size - 4; }

  struct I_Cache
  {
    struct Cache_Elem
    {
      Instruction instruction{ 0 };
      Instruction_Type type{};
    };

    constexpr I_Cache(const System &sys, const std::uint32_t t_start) noexcept : start(t_start) { fill_cache(sys); }

    constexpr Cache_Elem fetch(const std::uint32_t loc, const System &sys) noexcept
    {
      if (loc > start + (cache.size() * 4)) {
        start = loc;
        fill_cache(sys);
      }

      return cache[(loc - start) / 4];
    }

    constexpr void fill_cache(const System &sys) noexcept
    {
      auto loc = start;
      for (auto &elem : cache) {
        elem.instruction = Instruction{ sys.read_word(loc) };
        elem.type        = sys.decode(elem.instruction);
        loc += 4;
      }
    }

  private:
    std::uint32_t start{ 0 };

    std::array<Cache_Elem, 1024> cache;
  };

  I_Cache i_cache{ *this, 0 };

  template<typename Tracer = void (*)(const System &, std::uint32_t, Instruction)>
  constexpr void run(const std::uint32_t loc, Tracer &&tracer = [](const System &, const auto, const auto) {}) noexcept
  {
    setup_run(loc);
    while (operations_remaining()) { next_operation(tracer); }
  }

  [[nodiscard]] constexpr auto get_second_operand_shift_amount(const Data_Processing val) const noexcept
  {
    if (val.operand_2_immediate_shift()) {
      return val.operand_2_shift_amount();
    } else {
      return 0xFF & registers[val.operand_2_shift_register()];
    }
  }

  [[nodiscard]] constexpr auto shift_register(const bool c_flag, const Shift_Type type, std::uint32_t shift_amount, std::uint32_t value) const
    noexcept -> std::pair<bool, std::uint32_t>
  {
    switch (type) {
    case Shift_Type::Logical_Left:
      if (shift_amount == 0) {
        return { c_flag, value };
      } else {
        return { test_bit(value, (32 - shift_amount)), value << shift_amount };
      }
    case Shift_Type::Logical_Right:
      if (shift_amount == 0) {
        return { test_bit(value, 31), 0 };
      } else {
        return { test_bit(value, (shift_amount - 1)), value >> shift_amount };
      }
    case Shift_Type::Arithmetic_Right:
      if (shift_amount == 0) {
        const bool is_negative = test_bit(value, 31);
        return { is_negative, is_negative ? -1 : 0 };
      } else {
        return { test_bit(value, (shift_amount - 1)), (static_cast<std::int32_t>(value) >> shift_amount) };
      }
    case Shift_Type::Rotate_Right:
      if (shift_amount == 0) {
        // This is rotate_right_extended
        return { value & 1, (static_cast<std::uint32_t>(c_flag) << 31u) | (value >> 1u) };
      } else {
        return { test_bit(value, (shift_amount - 1)), (value >> shift_amount) | (value << (32 - shift_amount)) };
      }
    }
  }

  [[nodiscard]] constexpr auto get_second_operand(const Data_Processing val) const noexcept -> std::pair<bool, std::uint32_t>
  {
    if (val.immediate_operand()) {
      return { c_flag(), val.operand_2_immediate() };
    } else {
      const auto shift_amount = get_second_operand_shift_amount(val);
      return shift_register(c_flag(), val.operand_2_shift_type(), shift_amount, registers[val.operand_2_register()]);
    }
  }


  constexpr void process(const Load_And_Store_Multiple val) noexcept
  {
    const auto register_list        = val.register_list();
    const auto bits_set             = popcnt(register_list);

    auto [start_address, index_amount] = [&]() -> std::pair<std::uint32_t, std::int32_t> {
      if (val.pre_indexing() && val.up_indexing()) {
        // increment before
        return { registers[val.base_register()] + 4, 4 };
      } else if (!val.pre_indexing() && val.up_indexing()) {
        // increment after
        return { registers[val.base_register()], 4 };
      } else if (val.pre_indexing() && !val.up_indexing()) {
        // decrement before
        return { registers[val.base_register()] - (bits_set * 4), -4 };
      } else {
        // decrement after
        return { registers[val.base_register()] - (bits_set * 4) + 4, -4 };
      }
    }();

    const auto load = val.load();

    if (val.psr()) {
      abort();  // cannot handle PSR
    }


    for (int i = 0; i < 16; ++i) {
      if (test_bit(register_list, i)) {
        if (load) {
          registers[i] = read_word(start_address);
        } else {
          write_word(start_address, registers[i]);
        }
        start_address += index_amount;
      }
    }

    if (val.write_back()) { registers[val.base_register()] += bits_set * index_amount; }
  }

  constexpr auto offset(const Single_Data_Transfer val) const noexcept
  {
    const auto offset = [=]() -> std::int64_t {
      if (val.immediate_offset()) {
        return val.offset();
      } else {
        const auto offset_register = registers[val.offset_register()];
        // note: carry out seems to have no use with Single_Data_Transfer
        return shift_register(c_flag(), val.offset_shift_type(), val.offset_shift_amount(), offset_register).second;
      }
    }();

    if (val.up_indexing()) {
      return offset;
    } else {
      return -offset;
    }
  }

  constexpr void process(const Single_Data_Transfer val) noexcept
  {
    const std::int64_t index_offset = offset(val);
    const auto base_location        = registers[val.base_register()];
    const bool pre_indexed          = val.pre_indexing();

    const auto src_dest_register = val.src_dest_register();
    const auto indexed_location  = static_cast<std::uint32_t>(base_location + index_offset);

    if (val.byte_transfer()) {
      if (const auto location = pre_indexed ? indexed_location : base_location; val.load()) {
        registers[src_dest_register] = read_byte(location);
      } else {
        write_byte(location, static_cast<std::uint8_t>(registers[src_dest_register] & 0xFF));
      }
    } else {
      // word transfer
      if (const auto location = pre_indexed ? indexed_location : base_location; val.load()) {
        registers[src_dest_register] = read_word(location);
      } else {
        write_word(location, registers[src_dest_register]);
      }
    }

    if (!pre_indexed || val.write_back()) { registers[val.base_register()] = indexed_location; }
  }

  constexpr void process(const Data_Processing val) noexcept
  {
    const auto first_operand               = registers[val.operand_1_register()];
    const auto [carry_out, second_operand] = get_second_operand(val);
    const auto destination_register        = val.destination_register();
    auto &destination                      = registers[destination_register];

    const auto update_logical_flags = [=, &destination, carry_out = carry_out](const bool write, const auto result) {
      if (val.set_condition_code() && destination_register != 15) {
        c_flag(carry_out);
        z_flag((0xFFFFFFFF & result) == 0);
        n_flag(test_bit(result, 31));
      }

      if (write) { destination = result; }
    };

    // use 64 bit operations to be able to capture carry
    const auto arithmetic = [=,
                             &destination,
                             carry          = c_flag(),
                             first_operand  = static_cast<std::uint64_t>(first_operand),
                             second_operand = static_cast<std::uint64_t>(second_operand)](const bool write, const bool invert_carry, const auto op) {
      const auto result = op(first_operand, second_operand, carry);

      static_assert(std::is_same_v<std::decay_t<decltype(result)>, std::uint64_t>);

      if (val.set_condition_code() && destination_register != 15) {
        z_flag((0xFFFFFFFF & result) == 0);
        n_flag(result & (1u << 31));
        const bool carry_result = (result & (1ull << 32)) != 0;
        c_flag(invert_carry ? !carry_result : carry_result);

        const auto first_op_sign  = test_bit(static_cast<std::uint32_t>(first_operand), 31);
        const auto second_op_sign = test_bit(static_cast<std::uint32_t>(second_operand), 31);
        const auto result_sign    = test_bit(static_cast<std::uint32_t>(result), 31);

        v_flag((first_op_sign == second_op_sign) && (result_sign != first_op_sign));
      }

      if (write) { destination = static_cast<std::uint32_t>(result); }
    };


    switch (val.get_opcode()) {
    case OpCode::AND: return update_logical_flags(true, first_operand & second_operand);
    case OpCode::EOR: return update_logical_flags(true, first_operand ^ second_operand);
    case OpCode::TST: return update_logical_flags(false, first_operand & second_operand);
    case OpCode::TEQ: return update_logical_flags(false, first_operand ^ second_operand);
    case OpCode::ORR: return update_logical_flags(true, first_operand | second_operand);
    case OpCode::MOV: return update_logical_flags(true, second_operand);
    case OpCode::BIC: return update_logical_flags(true, first_operand & (~second_operand));
    case OpCode::MVN: return update_logical_flags(true, ~second_operand);

    case OpCode::SUB: return arithmetic(true, true, [](const auto op_1, const auto op_2, const auto) { return op_1 - op_2; });
    case OpCode::RSB: return arithmetic(true, true, [](const auto op_1, const auto op_2, const auto) { return op_2 - op_1; });
    case OpCode::ADD: return arithmetic(true, false, [](const auto op_1, const auto op_2, const auto) { return op_1 + op_2; });
    case OpCode::ADC: return arithmetic(true, false, [](const auto op_1, const auto op_2, const auto c) { return op_1 + op_2 + c; });
    case OpCode::SBC: return arithmetic(true, true, [](const auto op_1, const auto op_2, const auto c) { return op_1 - op_2 + c - 1; });
    case OpCode::RSC: return arithmetic(true, true, [](const auto op_1, const auto op_2, const auto c) { return op_2 - op_1 + c - 1; });
    case OpCode::CMP: return arithmetic(false, true, [](const auto op_1, const auto op_2, const auto) { return op_1 - op_2; });
    case OpCode::CMN: return arithmetic(false, false, [](const auto op_1, const auto op_2, const auto) { return op_1 + op_2; });
    }
  }

  constexpr void process(const Branch instruction) noexcept
  {
    if (instruction.link()) {
      // Link bit set, get PC, which is already pointing at the next instruction
      LR() = PC();
    }

    PC() += static_cast<std::uint32_t>(instruction.offset() + 4);
  }

  constexpr void process(const Multiply_Long val) noexcept
  {
    const auto result = [val, lhs = registers[val.operand_1()], rhs = registers[val.operand_2()]]() {
      if (val.unsigned_mul()) {
        return static_cast<std::uint64_t>(lhs) * static_cast<std::uint64_t>(rhs);
      } else {
        return static_cast<std::uint64_t>(static_cast<std::int64_t>(lhs) * static_cast<std::int64_t>(rhs));
      }
    }();

    if (val.accumulate()) {
      registers[val.high_result()] += static_cast<std::uint32_t>((result >> 32) & 0xFFFFFFFF);
      registers[val.low_result()] += static_cast<std::uint32_t>(result & 0xFFFFFFFF);
    } else {
      registers[val.high_result()] = static_cast<std::uint32_t>((result >> 32) & 0xFFFFFFFF);
      registers[val.low_result()]  = static_cast<std::uint32_t>(result & 0xFFFFFFFF);
    }

    if (val.status_register_update()) {
      z_flag(result == 0);
      n_flag(result & (static_cast<std::uint64_t>(1) << 63));
    }
  }

  constexpr static auto n_bit = 0b1000'0000'0000'0000'0000'0000'0000'0000;
  constexpr static auto z_bit = 0b0100'0000'0000'0000'0000'0000'0000'0000;
  constexpr static auto c_bit = 0b0010'0000'0000'0000'0000'0000'0000'0000;
  constexpr static auto v_bit = 0b0001'0000'0000'0000'0000'0000'0000'0000;

  constexpr static void set_or_clear_bit(std::uint32_t &val, const std::uint32_t bit, const bool set)
  {
    if (set) {
      val |= bit;
    } else {
      val &= ~bit;
    }
  }


  constexpr bool n_flag() const noexcept { return CSPR & n_bit; }
  constexpr void n_flag(const bool val) noexcept { set_or_clear_bit(CSPR, n_bit, val); }

  constexpr bool z_flag() const noexcept { return CSPR & z_bit; }
  constexpr void z_flag(const bool val) noexcept { set_or_clear_bit(CSPR, z_bit, val); }

  constexpr bool c_flag() const noexcept { return CSPR & c_bit; }
  constexpr void c_flag(const bool val) noexcept { set_or_clear_bit(CSPR, c_bit, val); }

  constexpr bool v_flag() const noexcept { return CSPR & v_bit; }
  constexpr void v_flag(const bool val) noexcept { set_or_clear_bit(CSPR, v_bit, val); }

  /// \sa Condition enumeration
  [[nodiscard]] constexpr bool check_condition(const Instruction instruction) const noexcept
  {
    switch (instruction.get_condition()) {
    case Condition::EQ: return z_flag();
    case Condition::NE: return !z_flag();
    case Condition::HS: return c_flag();
    case Condition::LO: return !c_flag();
    case Condition::MI: return n_flag();
    case Condition::PL: return !n_flag();
    case Condition::VS: return v_flag();
    case Condition::VC: return !v_flag();
    case Condition::HI: return c_flag() && !z_flag();
    case Condition::LS: return !c_flag() && z_flag();
    case Condition::GE: return (n_flag() && v_flag()) || (!n_flag() && !v_flag());
    case Condition::LT: return (n_flag() && !v_flag()) || (!n_flag() && v_flag());
    case Condition::GT: return !z_flag() && ((n_flag() && v_flag()) || (!n_flag() && !v_flag()));
    case Condition::LE: return z_flag() || (n_flag() && !v_flag()) || (!n_flag() && v_flag());
    case Condition::AL: return true;
    case Condition::NV:  // Reserved
      return false;
    };

    return false;
  }

  // make this constexpr static and it gets initialized exactly once, no question
  constexpr static auto lookup_table = get_lookup_table();

  [[nodiscard]] static constexpr auto decode(const Instruction instruction) noexcept
  {
    for (const auto &elem : lookup_table) {
      if ((std::get<0>(elem) & instruction) == std::get<1>(elem)) { return std::get<2>(elem); }
    }

    return Instruction_Type::Undefined;
  }

  constexpr void process(const Instruction instruction) noexcept { process(instruction, decode(instruction)); }

  constexpr void process(const Instruction instruction, const Instruction_Type type) noexcept
  {
    // account for prefetch
    PC() += 4;
    if (instruction.unconditional() || check_condition(instruction)) {
      switch (type) {
      case Instruction_Type::Data_Processing: process(Data_Processing{ instruction }); break;
      case Instruction_Type::Multiply_Long: process(Multiply_Long{ instruction }); break;
      case Instruction_Type::Single_Data_Transfer: process(Single_Data_Transfer{ instruction }); break;
      case Instruction_Type::Branch: process(Branch{ instruction }); break;
      case Instruction_Type::Load_And_Store_Multiple: process(Load_And_Store_Multiple{ instruction }); break;
      case Instruction_Type::MRS:
      case Instruction_Type::MSR:
      case Instruction_Type::MSRF:
      case Instruction_Type::Multiply:
      case Instruction_Type::Single_Data_Swap:
      case Instruction_Type::Undefined:
      case Instruction_Type::Block_Data_Transfer:
      case Instruction_Type::Coprocessor_Data_Transfer:
      case Instruction_Type::Coprocessor_Data_Operation:
      case Instruction_Type::Coprocessor_Register_Transfer:
      case Instruction_Type::Software_Interrupt: unhandled_instruction(instruction, type); break;
      }
    }

    // discount prefetch
    // PC() -= 4;
  }
};


}  // namespace arm_thing
