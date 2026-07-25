class Widget {
public:
  int value() const { return value_; }

private:
  int value_ = 1;
};

int read_widget(const Widget &widget) {
  return widget.value();
}
