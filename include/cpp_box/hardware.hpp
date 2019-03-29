#ifndef CPP_BOX_HARDWARE_HPP
#define CPP_BOX_HARDWARE_HPP

#include <cstring>
#include <cstdint>
#include <cstddef>

#include "memory_map.hpp"


namespace cpp_box {

template<typename T> auto peek(const std::uint32_t loc)
{
  auto p = reinterpret_cast<const std::byte *>(loc);
  T t;
  std::memcpy(&t, p, sizeof(t));
  asm volatile("" ::: "memory");
  return t;
}

template<typename T> inline void poke(const std::uint32_t loc, const T &t)
{
  auto p = reinterpret_cast<std::byte *>(loc);
  std::memcpy(p, &t, sizeof(t));
  asm volatile("" ::: "memory");
}

struct Hardware
{

  template<typename T> auto peek(const system::Memory_Map loc) { return cpp_box::peek<T>(static_cast<std::uint32_t>(loc)); }

  template<typename T> inline void poke(const system::Memory_Map loc, const T &t) { cpp_box::poke(static_cast<std::uint32_t>(loc), t); }

};

}  // namespace cpp_box

#endif
