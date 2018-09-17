namespace std {
  using uint8_t = unsigned char;
  using uint16_t = unsigned short;
  using uint32_t = unsigned int;
  using size_t = unsigned long long;
  using byte = unsigned char;
  
  
void memcpy(void *dest, const void *src, size_t size)
{
  auto dest_p = reinterpret_cast<std::byte *>(dest);
  auto src_p = reinterpret_cast<const std::byte *>(src);
  
  while (size) {
    *dest_p = *src_p;
    ++dest_p;
    ++src_p;
    --size;
  }
}

template<typename T>
const T &max(const T &lhs, const T &rhs) {
  if (lhs > rhs) { return lhs; }
  else {return rhs; }
}
}


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

struct RGBA
{
  std::uint8_t R;
  std::uint8_t G;
  std::uint8_t B;
  std::uint8_t A;
};

struct Display
{
  void write_pixel(int x, int y, RGBA val)
  {
    poke(1024 * 1024 * 8 + ((y * width) + x) * 4, val);
  }

  void set_resolution(std::uint16_t width, std::uint16_t height)
  {
    poke(4, static_cast<std::uint8_t>(width & 0xff));
    poke(5, static_cast<std::uint8_t>((width >> 8) & 0xff));
    poke(6, static_cast<std::uint8_t>(height & 0xff));
    poke(7, static_cast<std::uint8_t>((height >> 8) & 0xff));
  }


  Display() : Display(128, 128) {}

  Display(std::uint16_t t_width, std::uint16_t t_height)
    : width{ t_width }, height{ t_height }
  {
    set_resolution(width, height);
  }

  std::uint16_t width{ 128 };
  std::uint16_t height{ 128 };
};

int main()
{
  Display disp{ 64, 64 };

  for (std::uint8_t frame = 0; true; ++frame) {
    for (int x = 0; x < disp.width; ++x) {
      for (int y = 0; y < disp.height; ++y) {
        disp.write_pixel(
          x,
          y,
          RGBA{ static_cast<std::uint8_t>(frame - std::max(x, y)),
                static_cast<std::uint8_t>(frame - std::max(y, 255 - x)),
                static_cast<std::uint8_t>(frame - std::max(255 - y, x)),
                255 });
        disp.write_pixel(32, 32, RGBA{ 255, 255, 255, frame });
        disp.write_pixel(33, 32, RGBA{ 255, 255, frame, 255 });
        disp.write_pixel(33, 33, RGBA{ 255, frame, 255, 255 });
        disp.write_pixel(32, 33, RGBA{ frame, 255, 255, 255 });
      }
    }
  }
}
