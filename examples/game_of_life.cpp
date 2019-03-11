#include <hardware.hpp>
#include <algorithm>
#include <array>

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
    cpp_box::poke(1024 * 1024 * 8 + ((y * width) + x) * 4, val);
  }

  void set_resolution(std::uint16_t width, std::uint16_t height)
  {
    cpp_box::poke(4, static_cast<std::uint8_t>(width & 0xff));
    cpp_box::poke(5, static_cast<std::uint8_t>((width >> 8) & 0xff));
    cpp_box::poke(6, static_cast<std::uint8_t>(height & 0xff));
    cpp_box::poke(7, static_cast<std::uint8_t>((height >> 8) & 0xff));
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

template<std::size_t Cols, std::size_t Rows>
auto next(const std::array<std::array<bool, Cols>, Rows> &last)
{
  auto next_board                 = last;
  const auto neighbor_count = [&](const std::size_t col,
                                  const std::size_t row) {
    const auto start_col = col == 0 ? 0 : col - 1;
    const auto end_col   = col == Cols - 1 ? col : col + 1;
    const auto start_row = row == 0 ? 0 : row - 1;
    const auto end_row   = row == Rows - 1 ? row : row + 1;

    auto count = 0;
    for (auto cur_row = start_row; cur_row <= end_row; ++cur_row) {
      for (auto cur_col = start_col; cur_col <= end_col; ++cur_col) {
        if (cur_row != row && cur_col != col) {
          if (last[cur_row][cur_col]) { ++count; }
        }
      }
    }

    return count;
  };

  for (std::size_t col = 0; col < Cols; ++col) {
    for (std::size_t row = 0; row < Rows; ++row) {
      const auto num_neighbors = neighbor_count(col, row);

      if (num_neighbors == 3) {
        next_board[row][col] = true;
      } else if (num_neighbors < 2) {
        next_board[row][col] = false;
      } else if (num_neighbors > 3) {
        next_board[row][col] = false;
      }
    }
  }

  return next_board;
}

int main()
{
  Display disp{ 64, 64 };

  std::array<std::array<bool, 64>, 64> board{};

  board[20][20] = true;
  board[20][21] = true;
  board[20][22] = true;


  for (std::uint8_t frame = 0; true; ++frame) {
//    board = next(board);
    for (int x = 0; x < disp.width; ++x) {
      for (int y = 0; y < disp.height; ++y) {
        if (board[y][x]) {
          disp.write_pixel(x, y, RGBA{ 255, 255, 255, 255 });
        } else {
          disp.write_pixel(x, y, RGBA{ 0, 0, 0, 255 });
        }
      }
    }
  }
}
