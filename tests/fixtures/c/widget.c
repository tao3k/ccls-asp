typedef struct Widget {
  int value;
} Widget;

int widget_value(Widget *widget) {
  return widget->value;
}
