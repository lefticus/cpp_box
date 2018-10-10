#ifndef HEADER_FILE
#define HEADER_FILE

#include <array>
#include <cassert>
#include <iostream>


namespace cpp_box::elf {

template<std::size_t Bytes, typename Data>
[[nodiscard]] constexpr auto read_loc(const Data &data, const std::size_t loc, [[maybe_unused]] const bool little_endian) noexcept
{
  if constexpr (Bytes == 1) {
    return static_cast<std::uint8_t>(data[loc]);
  } else if constexpr (Bytes == 2) {
    const std::uint16_t byte0 = data[loc];
    const std::uint16_t byte1 = data[loc + 1];
    if (little_endian) {
      return static_cast<std::uint16_t>(byte0 | (byte1 << 8));
    } else {
      return static_cast<std::uint16_t>((byte0 << 8) | byte1);
    }
  } else {
    static_assert(Bytes == 1 || Bytes == 2 || Bytes == 4);
  }
}


struct Relocation_Entry
{
  std::basic_string_view<std::uint8_t> data;

  [[nodiscard]] constexpr auto read_file_offset() const noexcept -> std::uint64_t
  {
    return read_loc<2>(data, 0, true);
  }
};

}  // namespace cpp_box::elf

#endif
