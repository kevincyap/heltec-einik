#include <Arduino.h>
#include <driver/gpio.h>
#include "config.h"
#include "button.h"

static volatile bool _isr_pressed = false;
static volatile unsigned long _press_start_ms = 0;
static volatile unsigned long _last_edge_ms = 0;
static volatile uint8_t _pending_short = 0;
static volatile bool _pending_long = false;
static volatile bool _pending_menu = false;
static unsigned long _last_activity = 0;

static unsigned long isr_millis() {
  return (unsigned long)(micros() / 1000UL);
}

void IRAM_ATTR button_isr() {
  unsigned long now = isr_millis();
  if (now - _last_edge_ms < DEBOUNCE_MS / 2)
    return;
  _last_edge_ms = now;

  bool pressed = (gpio_get_level((gpio_num_t)USER_BUTTON_PIN) == 0);
  if (pressed && !_isr_pressed) {
    _isr_pressed = true;
    _press_start_ms = now;
    return;
  }

  if (!pressed && _isr_pressed) {
    _isr_pressed = false;
    unsigned long duration = now - _press_start_ms;

    if (duration >= MENU_PRESS_MS) {
      _pending_menu = true;
    } else if (duration >= LONG_PRESS_MS) {
      _pending_long = true;
    } else if (duration >= DEBOUNCE_MS) {
      if (_pending_short < 10)
        _pending_short++;
    }
  }
}

void button_init() {
  pinMode(USER_BUTTON_PIN, INPUT_PULLUP);
  _isr_pressed = (digitalRead(USER_BUTTON_PIN) == LOW);
  _press_start_ms = millis();
  _last_edge_ms = millis();
  _pending_short = 0;
  _pending_long = false;
  _pending_menu = false;
  attachInterrupt(digitalPinToInterrupt(USER_BUTTON_PIN), button_isr, CHANGE);
  _last_activity = millis();
}

ButtonEvent button_poll() {
  ButtonEvent evt = BTN_NONE;

  noInterrupts();
  if (_pending_menu) {
    _pending_menu = false;
    evt = BTN_MENU;
  } else if (_pending_long) {
    _pending_long = false;
    evt = BTN_LONG;
  } else if (_pending_short > 0) {
    _pending_short--;
    evt = BTN_SHORT;
  }
  interrupts();

  if (evt != BTN_NONE)
    _last_activity = millis();

  return evt;
}

unsigned long button_last_activity() {
  return _last_activity;
}

void button_inject_short_press() {
  noInterrupts();
  if (_pending_short < 10)
    _pending_short++;
  interrupts();
}
