#include <cassert>
#include <climits>
#include "mocks/diagnostics_stub.h"
#include "../../src/button.cpp"

unsigned long now_ms;
int pin_modes[49];
int pin_levels[49];
InterruptHandler interrupt_handlers[49];
SerialMock Serial;
esp_sleep_wakeup_cause_t mock_wake_cause;
bool slept;
uint64_t wake_mask;
int ext0_wake_pin;

static void reset() {
  now_ms = 1000;
  Serial.lines.clear();
  diagnostic_records.clear();
  for (int &level : pin_levels) level = HIGH;
  mock_wake_cause = ESP_SLEEP_WAKEUP_UNDEFINED;
  wake_mask = 0;
  ext0_wake_pin = -1;
  button_init();
}

#if INPUT_SINGLE_BUTTON
static void edge(int pin, int level, unsigned long elapsed = DEBOUNCE_MS) {
  now_ms += elapsed;
  pin_levels[pin] = level;
  interrupt_handlers[pin]();
}

static void tap(int pin, unsigned long duration = DEBOUNCE_MS) {
  edge(pin, LOW);
  edge(pin, HIGH, duration);
}
#endif

static void expect(ButtonEvent event) {
  assert(button_poll() == event);
  assert(button_poll() == BTN_NONE);
}

#if INPUT_SINGLE_BUTTON
static void expect_log(ButtonEvent event, int pin, unsigned long held,
                       unsigned long queued, unsigned long released) {
  char expected[160];
  snprintf(expected, sizeof(expected),
           "[INPUT] event=%d gpio=%u held=%lu queued=%lu released=%lu ms\n",
           (int)event, (unsigned)pin, held, queued, released);
  assert(!Serial.lines.empty() && Serial.lines.back() == expected);
}

static void test_chord(int face, bool modifier_first, bool modifier_released_first) {
  reset();
  edge(modifier_first ? USER_BUTTON_PIN : face, LOW);
  edge(modifier_first ? face : USER_BUTTON_PIN, LOW);
  edge(modifier_released_first ? USER_BUTTON_PIN : face, HIGH);
  edge(modifier_released_first ? face : USER_BUTTON_PIN, HIGH);
  expect(face == FACE_TOP_PIN ? BTN_MENU : BTN_NONE);
}

static void test_release_guard(int pin, ButtonEvent event, bool poll_during_bounce) {
  reset();
  tap(pin);
  unsigned long released = now_ms;
  if (poll_during_bounce) {
    assert(button_poll() == event);
    assert(button_last_event_time() == released);
  }
  edge(pin, LOW, 1);
  edge(pin, HIGH, 100);
  edge(pin, LOW, DEBOUNCE_MS - 1);
  edge(pin, HIGH, DEBOUNCE_MS);
  assert(button_last_activity() == now_ms);
  if (!poll_during_bounce) {
    assert(button_poll() == event);
    assert(button_last_event_time() == released);
    expect_log(event, pin, DEBOUNCE_MS, now_ms - released, released);
  }
  assert(button_poll() == BTN_NONE);
  tap(pin);
  expect(event);
}

static void test_release_guard_rollover() {
  reset();
  now_ms = ULONG_MAX - 100;
  edge(FACE_MIDDLE_PIN, LOW, 0);
  edge(FACE_MIDDLE_PIN, HIGH);
  edge(FACE_MIDDLE_PIN, LOW, 1);
  edge(FACE_MIDDLE_PIN, HIGH, 100);
  expect(BTN_SELECT);
  edge(FACE_MIDDLE_PIN, LOW, DEBOUNCE_MS - 1);
  edge(FACE_MIDDLE_PIN, HIGH);
  expect(BTN_NONE);
  tap(FACE_MIDDLE_PIN);
  expect(BTN_SELECT);
}

int main() {
  for (bool poll : {false, true}) {
    test_release_guard(USER_BUTTON_PIN, BTN_SHORT, poll);
    test_release_guard(FACE_TOP_PIN, BTN_UP, poll);
    test_release_guard(FACE_MIDDLE_PIN, BTN_SELECT, poll);
    test_release_guard(FACE_BOTTOM_PIN, BTN_DOWN, poll);
  }
  test_release_guard_rollover();
  reset();
  for (int pin : {USER_BUTTON_PIN, FACE_TOP_PIN, FACE_MIDDLE_PIN, FACE_BOTTOM_PIN})
    assert(pin_modes[pin] == INPUT_PULLUP && interrupt_handlers[pin]);
  tap(USER_BUTTON_PIN);
  expect(BTN_SHORT);
  tap(USER_BUTTON_PIN, LONG_PRESS_MS);
  expect(BTN_NONE);
  tap(USER_BUTTON_PIN, MENU_PRESS_MS + 100);
  expect(BTN_NONE);
  edge(USER_BUTTON_PIN, LOW, 1);
  edge(USER_BUTTON_PIN, HIGH);
  expect(BTN_NONE);
  tap(FACE_TOP_PIN);
  expect(BTN_UP);
  tap(FACE_MIDDLE_PIN);
  expect(BTN_SELECT);
  expect_log(BTN_SELECT, FACE_MIDDLE_PIN, DEBOUNCE_MS, 0, now_ms);
  assert(button_last_event_time() == now_ms);
  assert(diagnostic_records.back().when == now_ms);
  tap(FACE_BOTTOM_PIN);
  expect(BTN_DOWN);
  expect_log(BTN_DOWN, FACE_BOTTOM_PIN, DEBOUNCE_MS, 0, now_ms);
  tap(FACE_MIDDLE_PIN, MENU_PRESS_MS + 100);
  expect(BTN_SELECT);
  for (int face : {FACE_TOP_PIN, FACE_MIDDLE_PIN, FACE_BOTTOM_PIN})
    for (bool modifier_first : {false, true})
      for (bool release_first : {false, true})
        test_chord(face, modifier_first, release_first);

  reset();
  tap(USER_BUTTON_PIN, 5);
  tap(FACE_MIDDLE_PIN, 5);
  tap(FACE_MIDDLE_PIN, DEBOUNCE_MS - 1);
  expect(BTN_NONE);
  assert(Serial.lines.empty());
  tap(FACE_MIDDLE_PIN);
  expect(BTN_SELECT);
  edge(USER_BUTTON_PIN, LOW);
  edge(USER_BUTTON_PIN, HIGH, 5);
  edge(USER_BUTTON_PIN, LOW, 5);
  edge(USER_BUTTON_PIN, HIGH, DEBOUNCE_MS);
  expect(BTN_SHORT);

  reset();
  edge(USER_BUTTON_PIN, LOW);
  edge(FACE_TOP_PIN, LOW);
  edge(FACE_TOP_PIN, HIGH);
  edge(FACE_TOP_PIN, LOW, 1);
  edge(FACE_TOP_PIN, HIGH);
  edge(USER_BUTTON_PIN, HIGH);
  expect(BTN_MENU);

  reset();
  pin_levels[USER_BUTTON_PIN] = LOW;
  button_init();
  edge(USER_BUTTON_PIN, HIGH);
  edge(USER_BUTTON_PIN, LOW, 1);
  edge(USER_BUTTON_PIN, HIGH);
  expect(BTN_NONE);
  tap(USER_BUTTON_PIN);
  expect(BTN_SHORT);

  reset();
  tap(FACE_BOTTOM_PIN);
  unsigned long bottom_released = now_ms;
  tap(FACE_MIDDLE_PIN);
  unsigned long middle_released = now_ms;
  tap(FACE_TOP_PIN);
  assert(button_poll() == BTN_DOWN);
  expect_log(BTN_DOWN, FACE_BOTTOM_PIN, DEBOUNCE_MS,
             now_ms - bottom_released, bottom_released);
  assert(button_last_event_time() == bottom_released);
  assert(diagnostic_records.back().when == bottom_released);
  assert(button_poll() == BTN_SELECT);
  expect_log(BTN_SELECT, FACE_MIDDLE_PIN, DEBOUNCE_MS,
             now_ms - middle_released, middle_released);
  expect(BTN_UP);
  button_inject_short_press();
  expect(BTN_SHORT);
  assert(Serial.lines.back() == "[INPUT] event=1 source=injected queued=0 ms\n");
  for (int i = 0; i < EVENT_QUEUE_SIZE; i++)
    tap(FACE_MIDDLE_PIN);
  assert(_event_overflow);
  for (int i = 0; i < EVENT_QUEUE_SIZE - 1; i++)
    assert(button_poll() == BTN_SELECT);
  expect(BTN_NONE);
  assert(!_event_overflow);

  reset();
  now_ms += SLEEP_TIMEOUT_MS - 1;
  edge(USER_BUTTON_PIN, LOW);
  assert(button_last_activity() == now_ms);
  now_ms += SLEEP_TIMEOUT_MS * 2;
  assert(button_last_activity() == now_ms);
  edge(USER_BUTTON_PIN, HIGH);
  assert(button_last_activity() == now_ms);
  expect(BTN_NONE);

  reset();
  pin_levels[USER_BUTTON_PIN] = LOW;
  mock_wake_cause = ESP_SLEEP_WAKEUP_EXT0;
  unsigned long start = now_ms;
  assert(input_wait_wake_release() == 0 && now_ms == start);
  button_init();
  tap(FACE_TOP_PIN);
  expect(BTN_MENU);
  edge(USER_BUTTON_PIN, HIGH);
  expect(BTN_NONE);
  tap(USER_BUTTON_PIN);
  expect(BTN_SHORT);
  reset();
  pin_levels[USER_BUTTON_PIN] = LOW;
  button_init();
  edge(USER_BUTTON_PIN, HIGH);
  expect(BTN_NONE);
  tap(USER_BUTTON_PIN);
  expect(BTN_SHORT);
  pin_levels[FACE_MIDDLE_PIN] = LOW;
  button_init();
  edge(FACE_MIDDLE_PIN, HIGH);
  expect(BTN_NONE);

  input_enable_deep_sleep_wake();
  assert(ext0_wake_pin == USER_BUTTON_PIN);
  assert(wake_mask == (1ULL << USER_BUTTON_PIN));
  assert(input_woke_from_button(ESP_SLEEP_WAKEUP_EXT0));
  assert(!input_woke_from_button(ESP_SLEEP_WAKEUP_EXT1));
  puts("Heltec taps, layer chords, debounce, queued input, activity and wake tests passed");
}
#else
int main() {
  reset();
  for (int pin : {KEY_NEXT, KEY_PREV, KEY_OK, KEY_HOME, KEY_EXIT}) {
    pin_levels[pin] = LOW;
    now_ms += DEBOUNCE_MS + 1;
    assert(button_poll() == BTN_NONE);
    pin_levels[pin] = HIGH;
    now_ms += DEBOUNCE_MS + 1;
    ButtonEvent expected = pin == KEY_NEXT ? BTN_SHORT :
                          pin == KEY_PREV || pin == KEY_OK ? BTN_LONG : BTN_MENU;
    expect(expected);
  }
  input_enable_deep_sleep_wake();
  uint64_t expected = (1ULL << KEY_NEXT) | (1ULL << KEY_PREV) |
                      (1ULL << KEY_OK) | (1ULL << KEY_HOME) | (1ULL << KEY_EXIT);
  assert(wake_mask == expected && ext0_wake_pin == -1);
  assert(input_woke_from_button(ESP_SLEEP_WAKEUP_EXT1));
  assert(!input_woke_from_button(ESP_SLEEP_WAKEUP_EXT0));
  puts("CrowPanel key mappings and wake sources unchanged");
}
#endif
