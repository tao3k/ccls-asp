class Entity {
public:
  virtual ~Entity() = default;
};

class Widget : public Entity {
public:
  int value() const { return value_; }

private:
  int value_ = 1;
};

int read_widget(const Widget &widget) {
  return widget.value();
}
