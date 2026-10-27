#include <Arduino.h>
#include "serial_output.h"
#include <driver/gpio.h>
#include <esp_sleep.h>
#include "config.h"
#include "button.h"
#include "diagnostics.h"

static unsigned long _last_activity = 0;
static unsigned long _last_event_ms = 0;
static volatile uint8_t _pending_short = 0;

// =============================================================================
// Heltec USER modifier + face buttons. Capture releases even during rendering.
// =============================================================================
#if INPUT_SINGLE_BUTTON

static const uint8_t _pins[] = {
  USER_BUTTON_PIN, FACE_TOP_PIN, FACE_MIDDLE_PIN, FACE_BOTTOM_PIN
};
struct KeyState {
  bool pressed;
  bool modified;
  bool ignore_release;
  bool release_guard;
  unsigned long start;
  unsigned long released;
};
static volatile KeyState _keys[4];
static volatile bool _modifier_used = false;
static volatile unsigned long _input_activity = 0;
static const uint8_t EVENT_QUEUE_SIZE = 16;
static volatile ButtonEvent _events[EVENT_QUEUE_SIZE];
static volatile unsigned long _event_times[EVENT_QUEUE_SIZE];
static volatile uint8_t _event_pins[EVENT_QUEUE_SIZE];
static volatile unsigned long _event_durations[EVENT_QUEUE_SIZE];
static volatile uint8_t _event_read = 0;
static volatile uint8_t _event_write = 0;
static volatile bool _event_overflow = false;

static void IRAM_ATTR key_isr(uint8_t index) {
  unsigned long now = millis();
  volatile KeyState &key = _keys[index];
  bool pressed = gpio_get_level((gpio_num_t)_pins[index]) == 0;
  if (pressed == key.pressed)
    return;
  key.pressed = pressed;
  _input_activity = now;
  bool modifier = gpio_get_level((gpio_num_t)USER_BUTTON_PIN) == 0;
  if (pressed) {
    // A brief release glitch must not re-arm an already handled press.
    if (key.release_guard) {
      if (now - key.released < DEBOUNCE_MS)
        return;
      key.release_guard = false;
    }
    key.start = now;
    key.modified = index != 0 && modifier;
    if (index == 0) {
      _modifier_used = false;
      for (int i = 1; i < 4; i++) {
        if (_keys[i].pressed) {
          _keys[i].modified = true;
          _modifier_used = true;
        }
      }
    } else if (modifier) {
      _modifier_used = true;
    }
    return;
  }
  if (key.release_guard) {
    key.released = now;
    return;
  }
  if (key.ignore_release) {
    key.ignore_release = false;
    key.release_guard = true;
    key.released = now;
    return;
  }
  unsigned long duration = now - key.start;
  if (duration < DEBOUNCE_MS)
    return;
  key.release_guard = true;
  key.released = now;

  ButtonEvent event = BTN_NONE;
  if (index == 0) {
    if (!_modifier_used && duration < LONG_PRESS_MS)
      event = BTN_SHORT;
  } else {
    bool modified = key.modified || modifier;
    if (modifier) _modifier_used = true;
    if (modified) {
      if (index == 1) event = BTN_MENU;
    } else {
      event = index == 1 ? BTN_UP : index == 2 ? BTN_SELECT : BTN_DOWN;
    }
  }
  if (event != BTN_NONE) {
    uint8_t next = (_event_write + 1) % EVENT_QUEUE_SIZE;
    if (next == _event_read) {
      _event_overflow = true;
    } else {
      _events[_event_write] = event;
      _event_times[_event_write] = now;
      _event_pins[_event_write] = _pins[index];
      _event_durations[_event_write] = duration;
      _event_write = next;
    }
  }
}

static void IRAM_ATTR user_isr() { key_isr(0); }
static void IRAM_ATTR top_isr() { key_isr(1); }
static void IRAM_ATTR middle_isr() { key_isr(2); }
static void IRAM_ATTR bottom_isr() { key_isr(3); }

void button_init() {
  unsigned long now = millis();
  for (int i = 0; i < 4; i++) {
    pinMode(_pins[i], INPUT_PULLUP);
    _keys[i].pressed = digitalRead(_pins[i]) == LOW;
    _keys[i].modified = false;
    _keys[i].ignore_release = _keys[i].pressed;
    _keys[i].release_guard = false;
    _keys[i].start = now;
    _keys[i].released = now;
  }
  _modifier_used = false;
  _input_activity = now;
  _event_read = 0;
  _event_write = 0;
  _event_overflow = false;
  _pending_short = 0;
  attachInterrupt(digitalPinToInterrupt(USER_BUTTON_PIN), user_isr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(FACE_TOP_PIN), top_isr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(FACE_MIDDLE_PIN), middle_isr, CHANGE);
  attachInterrupt(digitalPinToInterrupt(FACE_BOTTOM_PIN), bottom_isr, CHANGE);
  _last_activity = now;
  _last_event_ms = now;
}

ButtonEvent button_poll() {
  ButtonEvent evt = BTN_NONE;
  unsigned long queued_ms = 0;
  unsigned long press_ms = 0;
  unsigned long released_ms = 0;
  uint8_t source_pin = 0;
  bool physical_event = false;

  noInterrupts();
  bool overflow = _event_overflow;
  _event_overflow = false;
  if (_event_read != _event_write) {
    evt = _events[_event_read];
    released_ms = _event_times[_event_read];
    queued_ms = millis() - released_ms;
    source_pin = _event_pins[_event_read];
    press_ms = _event_durations[_event_read];
    physical_event = true;
    _event_read = (_event_read + 1) % EVENT_QUEUE_SIZE;
  } else if (_pending_short > 0) {
    _pending_short--;
    evt = BTN_SHORT;
  }
  interrupts();
  if (overflow) {
    debug_serial.println("Button event queue overflow; input lost");
    diagnostics_log("ERROR input_queue_overflow");
  }
  if (physical_event) {
    debug_serial.printf("[INPUT] event=%d gpio=%u held=%lu queued=%lu released=%lu ms\n",
                  (int)evt, (unsigned)source_pin, press_ms, queued_ms, released_ms);
    diagnostics_log_at(released_ms, "INPUT event=%d gpio=%u held=%lu queued=%lu",
                       (int)evt, (unsigned)source_pin, press_ms, queued_ms);
  } else if (evt != BTN_NONE) {
    debug_serial.printf("[INPUT] event=%d source=injected queued=0 ms\n", (int)evt);
    diagnostics_log("INPUT event=%d source=injected", (int)evt);
  }

  if (evt != BTN_NONE) {
    _last_activity = millis();
    _last_event_ms = physical_event ? released_ms : _last_activity;
  }

  return evt;
}

void input_enable_deep_sleep_wake() {
  esp_sleep_enable_ext0_wakeup((gpio_num_t)USER_BUTTON_PIN, 0);
}

bool input_woke_from_button(esp_sleep_wakeup_cause_t cause) {
  return cause == ESP_SLEEP_WAKEUP_EXT0;
}

unsigned long input_wait_wake_release() {
  // button_init suppresses the wake press's release, without blocking its layer.
  return 0;
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
  _last_event_ms = _last_activity;
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
      _last_event_ms = now;
      diagnostics_log_at(now, "INPUT event=%d gpio=%u",
                         (int)_keys[i].evt, (unsigned)_keys[i].pin);
      return _keys[i].evt;
    }
  }

  if (_pending_short > 0) {
    _pending_short--;
    _last_activity = now;
    _last_event_ms = now;
    diagnostics_log_at(now, "INPUT event=%d source=injected", (int)BTN_SHORT);
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
#if INPUT_SINGLE_BUTTON
  noInterrupts();
  unsigned long activity = _input_activity;
  bool held = false;
  for (int i = 0; i < 4; i++)
    held = held || _keys[i].pressed;
  interrupts();
  if (held)
    _last_activity = millis();
  else {
    unsigned long now = millis();
    if (now - activity < now - _last_activity)
      _last_activity = activity;
  }
#endif
  return _last_activity;
}

unsigned long button_last_event_time() { return _last_event_ms; }

void button_inject_short_press() {
  noInterrupts();
  if (_pending_short < 10)
    _pending_short++;
  interrupts();
}
