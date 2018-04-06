#include <array>
#include <tuple>
#include <algorithm>
#include <iterator>

namespace ARM_Thing {
template<typename Value, typename Bit>[[nodiscard]] constexpr bool test_bit(const Value val, const Bit bit) noexcept
{
  return val & (static_cast<Value>(1) << bit);
}

template<typename Type, typename CRTP> struct Strongly_Typed
{
  [[nodiscard]] constexpr auto data() const noexcept { return m_val; }
  [[nodiscard]] constexpr auto operator&(const Type rhs) const noexcept { return m_val & rhs; }
  [[nodiscard]] constexpr bool test_bit(const Type bit) const noexcept { return ARM_Thing::test_bit(m_val, bit); }

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

  friend struct Data_Processing;
  friend struct Single_Data_Transfer;
  friend struct Multiply_Long;
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

  constexpr Single_Data_Transfer(Instruction ins) noexcept : Strongly_Typed{ ins.m_val } {}
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

  constexpr Multiply_Long(Instruction ins) noexcept : Strongly_Typed{ ins.m_val } {}
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
    const auto op_2  = operand_2();
    const auto value = static_cast<std::int8_t>(0b1111'1111 & op_2);

    // I believe this is defined behavior
    const auto sign_extended_value = static_cast<std::uint32_t>(static_cast<std::int32_t>(value));

    const auto shift_right = (op_2 >> 8) * 2;

    // Avoiding UB for shifts >= size
    if (shift_right == 0 || shift_right == 32) {
      return static_cast<std::uint32_t>(sign_extended_value);
    }

    // Should create a ROR on any modern compiler.
    // no branching, CMOV on GCC
    return (sign_extended_value >> shift_right) | (sign_extended_value << (32 - shift_right));
  }

  constexpr auto destination_register() const noexcept { return 0b1111 & (m_val >> 12); }

  [[nodiscard]] constexpr auto operand_1_register() const noexcept { return 0b1111 & (m_val >> 16); }
  [[nodiscard]] constexpr bool set_condition_code() const noexcept { return test_bit(20); }
  [[nodiscard]] constexpr bool immediate_operand() const noexcept { return test_bit(25); }


  constexpr Data_Processing(Instruction ins) noexcept : Strongly_Typed{ ins.m_val } {}
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
  Software_Interrupt
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
  std::array<std::tuple<std::uint32_t, std::uint32_t, Instruction_Type>, 15> table{
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
      { 0b0000'1111'0000'0000'0000'0000'0000'0000, 0b0000'1111'0000'0000'0000'0000'0000'0000, Instruction_Type::Software_Interrupt } }
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
  std::array<RAM_Block, 16> ram_map;
  std::array<ROM_Block, 16> rom_map;

  void invalid_memory_byte_write([[maybe_unused]] const std::uint32_t loc) const { std::terminate(); }
  void invalid_memory_word_write([[maybe_unused]] const std::uint32_t loc) const { std::terminate(); }

  std::uint8_t invalid_memory_byte_read([[maybe_unused]] const std::uint32_t loc) const { std::terminate(); }
  std::uint32_t invalid_memory_word_read([[maybe_unused]] const std::uint32_t loc) const { std::terminate(); }

  constexpr void unhandled_instruction([[maybe_unused]] const std::string_view description, [[maybe_unused]] const Instruction ins)
  {
    std::terminate();
  }

  template<typename Container> constexpr void set(Container &cont, const std::uint32_t start, const std::size_t ram_slot)
  {
    /// \todo check assumption that start + size <= std::integer_limits<std::uint32_t>::max
    ram_map[ram_slot] = { true, cont.data(), start, static_cast<std::uint32_t>(start + cont.size() - 1) };
  }

  template<typename Container> constexpr void set(const Container &cont, const std::uint32_t start, const std::size_t rom_slot)
  {
    /// \todo check assumption that start + size <= std::integer_limits<std::uint32_t>::max
    rom_map[rom_slot] = { true, cont.data(), start, static_cast<std::uint32_t>(start + cont.size() - 1) };
  }


  constexpr std::uint8_t read_byte(const std::uint32_t loc) const noexcept
  {
    for (auto &block : ram_map) {
      if (block.in_use && loc >= block.start && loc <= block.end) {
        return *(block.data + (loc - block.start));
      }
    }
    for (auto &block : rom_map) {
      if (block.in_use && loc >= block.start && loc <= block.end) {
        return *(block.data + (loc - block.start));
      }
    }
    if (loc < RAM_Size) {
      return builtin_ram[loc];
    }

    return invalid_memory_byte_read(loc);
  }

  constexpr void write_byte(const std::uint32_t loc, const std::uint8_t value) noexcept
  {
    for (auto &block : ram_map) {
      if (block.in_use && loc >= block.start && loc <= block.end) {
        block.data[loc - block.start] = value;
        return;
      }
    }
    if (loc < RAM_Size) {
      builtin_ram[loc] = value;
      return;
    }

    invalid_memory_byte_write(loc);
  }

  constexpr std::uint32_t read_word(const std::uint32_t loc) const noexcept
  {
    const std::uint8_t *data = [&]() -> const std::uint8_t * {
      for (const auto &block : ram_map) {
        if (block.in_use && loc >= block.start && loc + 3 <= block.end) {
          return block.data + (loc - block.start);
        }
      }
      for (const auto &block : rom_map) {
        if (block.in_use && loc >= block.start && loc + 3 <= block.end) {
          return block.data + (loc - block.start);
        }
      }
      if (loc + 3 < RAM_Size) {
        return &builtin_ram[loc];
      }
      return nullptr;
    }();

    if (data) {
      // Note: there are more efficient ways to do this with reinterpret_cast
      // or an intrinsic, but I cannot with constexpr context and I see
      // no compiler that can optimize around this implementation
      const std::uint32_t byte_1 = data[0];
      const std::uint32_t byte_2 = data[1];
      const std::uint32_t byte_3 = data[2];
      const std::uint32_t byte_4 = data[3];

      return byte_1 | (byte_2 << 8) | (byte_3 << 16) | (byte_4 << 24);
    } else {
      return invalid_memory_word_read(loc);
    }
  }

  constexpr void write_word(const std::uint32_t loc, const std::uint32_t value) noexcept
  {
    auto *data = [&]() -> std::uint8_t * {
      for (auto &block : ram_map) {
        if (block.in_use && loc >= block.start && loc + 3 <= block.end) {
          return block.data + (loc - block.start);
        }
      }
      if (loc + 3 <= RAM_Size) {
        return &builtin_ram[loc];
      }
      return nullptr;
    }();

    if (data) {
      data[0] = value & 0xFF;
      data[1] = (value >> 8) & 0xFF;
      data[2] = (value >> 16) & 0xFF;
      data[3] = (value >> 24) & 0xFF;
    } else {
      invalid_memory_word_write(loc);
    }
  }


  constexpr System() = default;

  template<std::size_t Size> constexpr System(const std::array<std::uint8_t, Size> &memory) noexcept
  {
    static_assert(Size <= RAM_Size);

    for (std::size_t loc = 0; loc < Size; ++loc) {
      // cast is safe - we verified this statically assigned RAM was the right size
      write_byte(static_cast<std::uint32_t>(loc), memory[loc]);
    }
  }

  constexpr Instruction get_instruction(const std::uint32_t PC) noexcept { return Instruction{ read_word(PC) }; }

  template<typename Tracer = void (*)(const System &)> constexpr void run(const std::uint32_t loc, Tracer &&tracer = [](const System &) {}) noexcept
  {
    // Set return from call register, so when 'main' returns,
    // we'll be at the end of RAM
    registers[14] = RAM_Size - 4;

    PC() = loc + 4;
    while (PC() != RAM_Size - 4) {
      tracer(*this);
      process(get_instruction(PC() - 4));
    }
  }

  [[nodiscard]] constexpr auto get_second_operand_shift_amount(const Data_Processing val) const noexcept
  {
    if (val.operand_2_immediate_shift()) {
      return val.operand_2_shift_amount();
    } else {
      return 0xFF & registers[val.operand_2_shift_register()];
    }
  }

  [[nodiscard]] constexpr std::pair<bool, std::uint32_t>
    shift_register(const bool c_flag, const Shift_Type type, std::uint32_t shift_amount, std::uint32_t value) const noexcept
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

  [[nodiscard]] constexpr std::pair<bool, std::uint32_t> get_second_operand(const Data_Processing val) const noexcept
  {
    if (val.immediate_operand()) {
      return { c_flag(), val.operand_2_immediate() };
    } else {
      const auto shift_amount = get_second_operand_shift_amount(val);
      return shift_register(c_flag(), val.operand_2_shift_type(), shift_amount, registers[val.operand_2_register()]);
    }
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

  constexpr void single_data_transfer(const Single_Data_Transfer val) noexcept
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

    if (!pre_indexed || val.write_back()) {
      registers[val.base_register()] = indexed_location;
    }
  }

  constexpr void data_processing(const Data_Processing val) noexcept
  {
    const auto first_operand              = registers[val.operand_1_register()];
    const auto[carry_out, second_operand] = get_second_operand(val);
    const auto destination_register       = val.destination_register();
    auto &destination                     = registers[destination_register];

    const auto update_logical_flags = [ =, &destination, carry_out = carry_out ](const bool write, const auto result)
    {
      if (val.set_condition_code() && destination_register != 15) {
        c_flag(carry_out);
        z_flag(result == 0);
        n_flag(test_bit(result, 31));
      }
      if (write) {
        destination = result;
      }
    };

    // use 64 bit operations to be able to capture carry
    const auto arithmetic =
      [ =, &destination, carry = c_flag(), first_operand = static_cast<std::uint64_t>(first_operand), second_operand = second_operand ](
        const bool write, const auto op)
    {
      const auto result = op(first_operand, second_operand, carry);

      static_assert(std::is_same_v<std::decay_t<decltype(result)>, std::uint64_t>);

      if (val.set_condition_code() && destination_register != 15) {
        z_flag(result == 0);
        n_flag(result & (1u << 31));
        c_flag(result & (1ull << 32));

        const auto first_op_sign  = test_bit(static_cast<std::uint32_t>(first_operand), 31);
        const auto second_op_sign = test_bit(static_cast<std::uint32_t>(second_operand), 31);
        const auto result_sign    = test_bit(static_cast<std::uint32_t>(result), 31);

        v_flag((first_op_sign == second_op_sign) && (result_sign != first_op_sign));
      }

      if (write) {
        destination = static_cast<std::uint32_t>(result);
      }
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

    case OpCode::SUB: return arithmetic(true, [](const auto op_1, const auto op_2, const auto) { return op_1 - op_2; });
    case OpCode::RSB: return arithmetic(true, [](const auto op_1, const auto op_2, const auto) { return op_2 - op_1; });
    case OpCode::ADD: return arithmetic(true, [](const auto op_1, const auto op_2, const auto) { return op_1 + op_2; });
    case OpCode::ADC: return arithmetic(true, [](const auto op_1, const auto op_2, const auto c) { return op_1 + op_2 + c; });
    case OpCode::SBC: return arithmetic(true, [](const auto op_1, const auto op_2, const auto c) { return op_1 - op_2 + c - 1; });
    case OpCode::RSC: return arithmetic(true, [](const auto op_1, const auto op_2, const auto c) { return op_2 - op_1 + c - 1; });
    case OpCode::CMP: return arithmetic(false, [](const auto op_1, const auto op_2, const auto) { return op_1 - op_2; });
    case OpCode::CMN: return arithmetic(false, [](const auto op_1, const auto op_2, const auto) { return op_1 + op_2; });
    }
  }

  constexpr void branch(const Instruction instruction) noexcept
  {
    if (instruction.test_bit(24)) {
      // Link bit set, get previous PC
      registers[14] = PC() - 4;
    }

    const auto offset = [](const auto op) -> std::int32_t {
      if (op.test_bit(23)) {
        // is signed
        const auto twos_compliment = ((~(op & 0x00FFFFFFF)) + 1) & 0x00FFFFFF;
        return -static_cast<int32_t>(twos_compliment << 2);
      } else {
        return static_cast<int32_t>((op & 0x00FFFFFF) << 2);
      }
    }(instruction);

    PC() += offset + 4;
  }

  constexpr void multiply_long(const Multiply_Long val) noexcept
  {
    const auto result = [ val, lhs = registers[val.operand_1()], rhs = registers[val.operand_2()] ]()
    {
      if (val.unsigned_mul()) {
        return static_cast<std::uint64_t>(lhs) * static_cast<std::uint64_t>(rhs);
      } else {
        return static_cast<std::uint64_t>(static_cast<std::int64_t>(lhs) * static_cast<std::int64_t>(rhs));
      }
    }
    ();

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
  }

  // make this constexpr static and it gets initialized exactly once, no question
  constexpr static auto lookup_table = get_lookup_table();

  [[nodiscard]] constexpr auto decode(const Instruction instruction) noexcept
  {
    for (const auto &elem : lookup_table) {
      if ((std::get<0>(elem) & instruction) == std::get<1>(elem)) {
        return std::get<2>(elem);
      }
    }

    return Instruction_Type::Undefined;
  }

  constexpr void process(const Instruction instruction) noexcept
  {
    // account for prefetch
    PC() += 4;
    if (check_condition(instruction)) {
      switch (decode(instruction)) {
      case Instruction_Type::Data_Processing: data_processing(instruction); break;
      case Instruction_Type::MRS: unhandled_instruction("MRS", instruction); break;
      case Instruction_Type::MSR: unhandled_instruction("MSR", instruction); break;
      case Instruction_Type::MSRF: unhandled_instruction("MSR flags", instruction); break;
      case Instruction_Type::Multiply: unhandled_instruction("Multiply", instruction); break;
      case Instruction_Type::Multiply_Long: multiply_long(instruction); break;
      case Instruction_Type::Single_Data_Swap: unhandled_instruction("Single_Data_Swap", instruction); break;
      case Instruction_Type::Single_Data_Transfer: single_data_transfer(instruction); break;
      case Instruction_Type::Undefined: unhandled_instruction("Undefined", instruction); break;
      case Instruction_Type::Block_Data_Transfer: unhandled_instruction("Block_Data_Transfer", instruction); break;
      case Instruction_Type::Branch: branch(instruction); break;
      case Instruction_Type::Coprocessor_Data_Transfer: unhandled_instruction("Coprocessor_Data_Transfer", instruction); break;
      case Instruction_Type::Coprocessor_Data_Operation: unhandled_instruction("Coprocessor_Data_Operation", instruction); break;
      case Instruction_Type::Coprocessor_Register_Transfer: unhandled_instruction("Coprocessor_Register_Transfer", instruction); break;
      case Instruction_Type::Software_Interrupt: unhandled_instruction("Software_Interrupt", instruction); break;
      }
    }

    // discount prefetch
    //PC() -= 4;
  }
};  // namespace ARM_Thing


}  // namespace ARM_Thing
