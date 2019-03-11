#include <cstdint>

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
  } else if constexpr (Bytes == 4) {
    const std::uint32_t byte0 = data[loc];
    const std::uint32_t byte1 = data[loc + 1];
    const std::uint32_t byte2 = data[loc + 2];
    const std::uint32_t byte3 = data[loc + 3];
    if (little_endian) {
      return byte0 | (byte1 << 8) | (byte2 << 16) | (byte3 << 24);
    } else {
      return (byte0 << 24) | (byte1 << 16) | (byte2 << 8) | byte3;
    }
  } else if constexpr (Bytes == 8) {
    const std::uint64_t byte0 = data[loc];
    const std::uint64_t byte1 = data[loc + 1];
    const std::uint64_t byte2 = data[loc + 2];
    const std::uint64_t byte3 = data[loc + 3];
    const std::uint64_t byte4 = data[loc + 4];
    const std::uint64_t byte5 = data[loc + 5];
    const std::uint64_t byte6 = data[loc + 6];
    const std::uint64_t byte7 = data[loc + 7];
    if (little_endian) {
      return byte0 | (byte1 << 8) | (byte2 << 16) | (byte3 << 24) | (byte4 << 32) | (byte5 << 40) | (byte6 << 48) | (byte7 << 56);
    } else {
      return (byte0 << 56) | (byte1 << 48) | (byte2 << 40) | (byte3 << 32) | (byte4 << 24) | (byte5 << 16) | (byte6 << 8) | byte7;
    }
  } else {
    static_assert(Bytes == 1 || Bytes == 2 || Bytes == 4 || Bytes == 8);
  }
}
}
