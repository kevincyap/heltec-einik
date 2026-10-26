#include <cassert>
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
static void test_chord(int face, bool modifier_first, bool modifier_released_first) {
  reset();
  edge(modifier_first ? USER_BUTTON_PIN : face, LOW);
  edge(modifier_first ? face : USER_BUTTON_PIN, LOW);
  edge(modifier_released_first ? USER_BUTTON_PIN : face, HIGH);
  edge(modifier_released_first ? face : USER_BUTTON_PIN, HIGH);
  expect(face == FACE_TOP_PIN ? BTN_MENU : BTN_NONE);
}

int main() {
  reset();
  for (int pin : {USER_BUTTON_PIN, FACE_TOP_PIN, FACE_MIDDLE_PIN, FACE_BOTTOM_PIN})
    assert(pin_modes[pin] == INPUT_PULLUP && interrupt_handlers[pin]);
  tap(USER_BUTTON_PIN);
  expect(BTN_SHORT);
  tap(USER_BUTTON_PIN, LONG_PRESS_MS);
  expect(BTN_NONE);
  tap(USER_BUTTON_PIN, MENU_PRESS_MS + 100);
  expect(BTN_NONE);
  tap(FACE_TOP_PIN);
  expect(BTN_UP);
  tap(FACE_MIDDLE_PIN);
  expect(BTN_SELECT);
  tap(FACE_BOTTOM_PIN);
  expect(BTN_DOWN);
  tap(FACE_MIDDLE_PIN, MENU_PRESS_MS + 100);
  expect(BTN_SELECT);
  for (int face : {FACE_TOP_PIN, FACE_MIDDLE_PIN, FACE_BOTTOM_PIN})
    for (bool modifier_first : {false, true})
      for (bool release_first : {false, true})
        test_chord(face, modifier_first, release_first);

  reset();
  tap(USER_BUTTON_PIN, 5);
  tap(FACE_MIDDLE_PIN, 5);
  expect(BTN_NONE);
  tap(FACE_MIDDLE_PIN);
  expect(BTN_SELECT);
  edge(USER_BUTTON_PIN, LOW);
  edge(USER_BUTTON_PIN, HIGH, 5);
  edge(USER_BUTTON_PIN, LOW, 5);
  edge(USER_BUTTON_PIN, HIGH, DEBOUNCE_MS);
  expect(BTN_SHORT);

  reset();
  tap(FACE_BOTTOM_PIN);
  tap(FACE_MIDDLE_PIN);
  tap(FACE_TOP_PIN);
  assert(button_poll() == BTN_DOWN);
  assert(button_poll() == BTN_SELECT);
  expect(BTN_UP);
  button_inject_short_press();
  expect(BTN_SHORT);
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
