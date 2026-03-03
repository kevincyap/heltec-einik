#include <Arduino.h>
#include "config.h"
#include "button.h"

static unsigned long _press_start = 0;
static bool _was_pressed = false;
static unsigned long _last_activity = 0;

void button_init() {
  pinMode(USER_BUTTON_PIN, INPUT_PULLUP);
  _last_activity = millis();
}

ButtonEvent button_poll() {
  bool pressed = (digitalRead(USER_BUTTON_PIN) == LOW);
  unsigned long now = millis();

  if (pressed && !_was_pressed) {
    _press_start = now;
    _was_pressed = true;
  } else if (!pressed && _was_pressed) {
    _was_pressed = false;
    unsigned long duration = now - _press_start;
    _last_activity = now;

    if (duration >= MENU_PRESS_MS)
      return BTN_MENU;
    if (duration >= LONG_PRESS_MS)
      return BTN_LONG;
    if (duration >= DEBOUNCE_MS)
      return BTN_SHORT;
  }

  return BTN_NONE;
}

unsigned long button_last_activity() {
  return _last_activity;
}
