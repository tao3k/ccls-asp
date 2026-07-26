#include "widget.h"

static int bump(int value) { return WIDGET_INCREMENT(value); }

int widget_value(Widget *widget) { return bump(widget->value); }
