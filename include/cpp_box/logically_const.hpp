#include <utility>

// hard to assign to, local to a specific type
// fully reusable
template<typename T, typename Friend>
class logically_const
{
public:
  [[nodiscard]] constexpr const T &value() const noexcept { return data; }

  [[nodiscard]] constexpr decltype(auto) operator*() const noexcept { return value(); }
  [[nodiscard]] constexpr decltype(auto) operator->() const noexcept { return value(); }
  
  constexpr operator const T & () const noexcept {
    return value();
  }

  constexpr logically_const(const T &t) = delete;
  constexpr logically_const(T &&t) : data{std::move(t)} {}
  T& operator=(const T &) = delete;
  T& operator=(T &&) = delete;
  
private:
  T data;
  logically_const() = delete;
  constexpr logically_const &operator=(const logically_const &) = default;
  constexpr logically_const &operator=(logically_const &&) = default;
  friend Friend;
};

struct ThingWithConstData
{
  logically_const<int, ThingWithConstData> value;
};

int main()
{
  // directly initializable
  ThingWithConstData d1{15};

  // constexpr capable
  constexpr ThingWithConstData d2{17};

  // asignable
  d1 = d2;

  // accessible
  return d1.value;

  // hard to modify
  //d1.value = 15;
  //d1.value = logically_const<int, ThingWithConstData>{10};
}




