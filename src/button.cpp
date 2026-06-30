#include <Arduino.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include "config.h"
#include "button.h"

static unsigned long _last_activity = 0;
static volatile uint8_t _pending_short = 0;

// =============================================================================
// Single USER button (Heltec Vision Master E213) — ISR + duration decoding
// =============================================================================
#if INPUT_SINGLE_BUTTON

static volatile bool _isr_pressed = false;
static volatile unsigned long _press_start_ms = 0;
static volatile unsigned long _last_edge_ms = 0;
static volatile bool _pending_long = false;
static volatile bool _pending_menu = false;

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

void input_enable_deep_sleep_wake() {
  esp_sleep_enable_ext0_wakeup((gpio_num_t)USER_BUTTON_PIN, 0);
}

bool input_woke_from_button(esp_sleep_wakeup_cause_t cause) {
  return cause == ESP_SLEEP_WAKEUP_EXT0;
}

unsigned long input_wait_wake_release() {
  pinMode(USER_BUTTON_PIN, INPUT_PULLUP);
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT0)
    return 0;

  unsigned long start = millis();
  while (digitalRead(USER_BUTTON_PIN) == LOW &&
         (millis() - start) < WAKE_BUTTON_RELEASE_TIMEOUT_MS) {
    delay(10);
  }
  return millis() - start;
}

// =============================================================================
// Discrete keys + rotary dial (Elecrow CrowPanel) — polled edge detection
// =============================================================================
#else

struct KeyMap {
  uint8_t pin;
  ButtonEvent evt;
};

// Active-low keys (idle HIGH via on-board pull-ups). Mapping onto the existing
// next / prev / menu actions:
//   NEXT       -> BTN_SHORT (next page / menu scroll)
//   PREV, OK   -> BTN_LONG  (prev page / menu select)
//   HOME, EXIT -> BTN_MENU  (open menu)
static const KeyMap _keys[] = {
  {KEY_NEXT, BTN_SHORT},
  {KEY_PREV, BTN_LONG},
  {KEY_OK,   BTN_LONG},
  {KEY_HOME, BTN_MENU},
  {KEY_EXIT, BTN_MENU},
};
static const int _key_count = sizeof(_keys) / sizeof(_keys[0]);

static bool _pressed[sizeof(_keys) / sizeof(_keys[0])] = {false};
static unsigned long _last_change[sizeof(_keys) / sizeof(_keys[0])] = {0};

static uint64_t wake_pin_mask() {
  uint64_t mask = 0;
  for (int i = 0; i < _key_count; i++)
    mask |= (1ULL << _keys[i].pin);
  return mask;
}

void button_init() {
  for (int i = 0; i < _key_count; i++) {
    pinMode(_keys[i].pin, INPUT);
    _pressed[i] = (digitalRead(_keys[i].pin) == LOW);
    _last_change[i] = millis();
  }
  _pending_short = 0;
  _last_activity = millis();
}

ButtonEvent button_poll() {
  unsigned long now = millis();

  for (int i = 0; i < _key_count; i++) {
    bool low = (digitalRead(_keys[i].pin) == LOW);

    if (low && !_pressed[i] && (now - _last_change[i]) > DEBOUNCE_MS) {
      _pressed[i] = true;
      _last_change[i] = now;
    } else if (!low && _pressed[i] && (now - _last_change[i]) > DEBOUNCE_MS) {
      // Emit the mapped event on release.
      _pressed[i] = false;
      _last_change[i] = now;
      _last_activity = now;
      return _keys[i].evt;
    }
  }

  if (_pending_short > 0) {
    _pending_short--;
    _last_activity = now;
    return BTN_SHORT;
  }

  return BTN_NONE;
}

void input_enable_deep_sleep_wake() {
  esp_sleep_enable_ext1_wakeup(wake_pin_mask(), ESP_EXT1_WAKEUP_ANY_LOW);
}

bool input_woke_from_button(esp_sleep_wakeup_cause_t cause) {
  return cause == ESP_SLEEP_WAKEUP_EXT1;
}

unsigned long input_wait_wake_release() {
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT1)
    return 0;

  for (int i = 0; i < _key_count; i++)
    pinMode(_keys[i].pin, INPUT);

  unsigned long start = millis();
  bool any_low = true;
  while (any_low && (millis() - start) < WAKE_BUTTON_RELEASE_TIMEOUT_MS) {
    any_low = false;
    for (int i = 0; i < _key_count; i++) {
      if (digitalRead(_keys[i].pin) == LOW)
        any_low = true;
    }
    if (any_low)
      delay(10);
  }
  return millis() - start;
}

#endif

// =============================================================================
// Shared
// =============================================================================
unsigned long button_last_activity() {
  return _last_activity;
}

void button_inject_short_press() {
  noInterrupts();
  if (_pending_short < 10)
    _pending_short++;
  interrupts();
}
