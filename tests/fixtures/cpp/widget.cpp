class Entity {
public:
  virtual ~Entity() = default;
  virtual int value() const = 0;
};

class Widget : public Entity {
public:
  int value() const override { return value_; }

private:
  int value_ = 1;
};

int read_widget(const Widget &widget) { return widget.value(); }

template <typename T> class Box {
public:
  explicit Box(T value) : value(value) {}
  T value;
};

template <typename T> T identity(T value) { return value; }

namespace detail {
int twice(int value) { return value * 2; }
}

namespace implementation = detail;
using detail::twice;

int invoke(int (*function)(int), int value) { return function(value); }

int advanced_widget() {
  Box<int> box(2);
  auto lambda = [](int value) { return value + 1; };
  return lambda(identity(implementation::twice(box.value)));
}
