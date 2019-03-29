#include <fmt/format.h>

#include <rang.hpp>

namespace cpp_box::utility {
template<typename Cont> void dump_rom(const Cont &c)
{
  std::size_t loc = 0;
  std::clog << rang::fg::yellow << fmt::format("Dumping Data At Loc: {}\n", static_cast<const void *>(c.data())) << rang::style::dim;

  for (const auto byte : c) {
    std::clog << fmt::format(" {:02x}", byte);
    if ((++loc % 16) == 0) { std::clog << '\n'; }
  }
  std::clog << '\n' << rang::style::reset << rang::fg::reset;
}
}  // namespace cpp_box::utility
