typedef struct Widget {
  int value;
} Widget;

static int bump(int value) { return value + 1; }

int widget_value(Widget *widget) { return bump(widget->value); }
