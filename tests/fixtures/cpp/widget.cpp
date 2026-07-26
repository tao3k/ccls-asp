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
