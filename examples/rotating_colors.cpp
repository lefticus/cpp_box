#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>

template <typename T>
auto peek(const std::uint32_t loc) {
  auto p = reinterpret_cast<const std::byte *>(loc);
  T t;
  std::memcpy(&t, p, sizeof(t));
  asm volatile("" ::: "memory");
  return t;
}

template <typename T>
inline void poke(const std::uint32_t loc, const T &t) {
  auto p = reinterpret_cast<std::byte *>(loc);
  std::memcpy(p, &t, sizeof(t));
  asm volatile("" ::: "memory");
}

struct RGBA {
  std::uint8_t R;
  std::uint8_t G;
  std::uint8_t B;
  std::uint8_t A;
};

void write_pixel(int x, int y, RGBA val) {
  poke(1024*1024*8 + ((y * 256) + x) * 4, val);
}

int main() {
  //  std::array<RGB, 256 * 256> buffer{};
  poke(4, std::uint8_t{128});
  poke(5, std::uint8_t{0});
  for (std::uint8_t frame = 0; true; ++frame) {
    for (int x = 0; x < 256; ++x) {
      for (int y = 0; y < 256; ++y) {
        write_pixel(
            x, y,
            RGBA{static_cast<std::uint8_t>(frame - std::max(x, y)),
                 static_cast<std::uint8_t>(frame - std::max(y, 255 - x)),
                 static_cast<std::uint8_t>(frame - std::max(255 - y, x)), 255});
      }
    }
  }
  /*
   for (std::uint8_t frame = 0; true; ++frame) {
     for (int x = 0; x < 256; ++x) {
       for (int y = 0; y < 256; ++y) {
         buffer[y * 256 + x] =
             RGBA{static_cast<std::uint8_t>(frame - std::max(x, y)),
                  static_cast<std::uint8_t>(frame - std::max(y, 255 - x)),
                  static_cast<std::uint8_t>(frame - std::max(255 - y, x)), 255};
       }
     }
     poke(0x10000, buffer);
   }
   */
  // write_pixel(0,0,RGBA{255,255,255,255});
  // poke(0x4000, RGBA{255,255,255,255});
}
