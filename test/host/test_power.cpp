#include <cassert>
#define ARDUINO_USB_CDC_ON_BOOT 1
#include "../../src/main.cpp"

unsigned long now_ms;
int pin_modes[49];
int pin_levels[49];
SerialMock Serial;
LittleFSMock LittleFS;
bool vext_held;
bool deep_sleep_hold;
bool slept;
esp_sleep_wakeup_cause_t mock_wake_cause;

static int page;
static int renders;
static std::vector<bool> sleep_frames;
static int cache_loads;
static bool fast_open_ok;
static unsigned long wake_hold_ms;
static ButtonEvent event;
static unsigned long last_activity;

void button_init() { last_activity = now_ms; }
ButtonEvent button_poll() {
  ButtonEvent result = event;
  event = BTN_NONE;
  if (result != BTN_NONE) last_activity = now_ms;
  return result;
}
unsigned long button_last_activity() { return last_activity; }
void input_enable_deep_sleep_wake() {}
bool input_woke_from_button(esp_sleep_wakeup_cause_t cause) {
  return cause == ESP_SLEEP_WAKEUP_EXT0;
}
unsigned long input_wait_wake_release() { return wake_hold_ms; }
bool reader_is_open() { return true; }
const char *reader_filename() { return "/book.txt"; }
int reader_current_page() { return page; }
void reader_close() {}
bool reader_open(EinkDisplay &, const char *, int start) { page = start; return true; }
bool reader_open_fast(const char *, uint32_t, int, int start, const int[4]) {
  page = start;
  return fast_open_ok;
}
void reader_render(EinkDisplay &, bool sleeping) {
  renders++;
  sleep_frames.push_back(sleeping);
}
bool reader_next_page() { if (page == 9) return false; page++; return true; }
bool reader_prev_page() { if (page == 0) return false; page--; return true; }
void reader_load_full_cache(int, int) { cache_loads++; }
void reader_get_page_window(int *, int *, uint32_t *, int[4]) {}
ReadingState state_load() {
  ReadingState result = {};
  strcpy(result.filename, "/book.txt");
  result.page = page;
  result.valid = true;
  return result;
}
void state_save(const char *, int) {}
int state_load_book_page(const char *) { return page; }
void state_save_book_page(const char *, int) {}
bool menu_show(EinkDisplay &, char *, size_t) { return false; }
void wifi_upload_start(EinkDisplay &) {}
void wifi_upload_tick() {}
void wifi_upload_stop() {}
bool wifi_upload_active() { return false; }

struct TestDisplay : EinkDisplay {
  using LCMEN2R13EFC1::begun;
  using LCMEN2R13EFC1::buffer;
  using LCMEN2R13EFC1::commands;
  using LCMEN2R13EFC1::fastmode_secondpass;
  using LCMEN2R13EFC1::fastmode_state;
  using LCMEN2R13EFC1::spi;
  using EinkDisplay::sendImageData;
};

static void reset() {
  now_ms = 1000;
  last_activity = now_ms;
  page = 4;
  renders = cache_loads = 0;
  sleep_frames.clear();
  slept = false;
  event = BTN_NONE;
  wake_hold_ms = 0;
  fast_open_ok = true;
  mock_wake_cause = ESP_SLEEP_WAKEUP_EXT0;
  _sleep_after_turn_pending = false;
  _wake_turn_page_pending = true;
  _rtc_reader_locked = false;
  strcpy(_rtc_filename, "/book.txt");
  _rtc_num_pages = 10;
  _rtc_page = page;
  _rtc_file_size = 10000;
  for (int &level : pin_levels) level = HIGH;
}

static void test_wake(bool fast, bool held) {
  reset();
  fast_open_ok = fast;
  wake_hold_ms = held ? MENU_PRESS_MS : 0;
  setup();
  assert(page == 5);
  assert(renders == 2);
  assert(cache_loads == (fast ? 1 : 0));
  assert(_sleep_after_turn_pending);
  now_ms = last_activity + SLEEP_TIMEOUT_MS - 1;
  loop();
  assert(!slept);
  now_ms = last_activity + SLEEP_TIMEOUT_MS;
  loop();
  assert(slept && _wake_turn_page_pending);
  assert(_rtc_reader_locked && sleep_frames.back());
}

static void test_sleep_lock_wake(bool fast, bool boundary, bool advance) {
  reset();
  fast_open_ok = fast;
  page = _rtc_page = boundary ? 9 : 4;
  _wake_turn_page_pending = advance;
  _rtc_reader_locked = true;
  setup();
  assert(sleep_frames.size() == 2);
  assert(sleep_frames[0] && !sleep_frames[1]);
  assert(!_rtc_reader_locked);
  assert(page == (advance && !boundary ? 5 : boundary ? 9 : 4));
}

static void test_boundary(bool fast, bool held) {
  reset();
  fast_open_ok = fast;
  page = _rtc_page = 9;
  wake_hold_ms = held ? MENU_PRESS_MS : 0;
  setup();
  assert(renders == 1);
  assert(!_sleep_after_turn_pending);
}

static void test_awake_turn(ButtonEvent button) {
  reset();
  _wake_turn_page_pending = false;
  event = button;
  loop();
  assert(page == (button == BTN_SHORT || button == BTN_SELECT ? 5 : 3));
  assert(_sleep_after_turn_pending);
}

static void test_activity_refresh(ButtonEvent button, bool boundary) {
  reset();
  setup();
  if (boundary) page = button == BTN_SHORT ? 9 : 0;
  int old_page = page;
  int old_renders = renders;
  now_ms = last_activity + SLEEP_TIMEOUT_MS - 1;
  event = button;
  loop();
  assert(!slept);
  if (button != BTN_MENU) {
    assert(page == old_page + (boundary ? 0 : button == BTN_SHORT ? 1 : -1));
    assert(renders == old_renders + (boundary ? 0 : 1));
  }
  now_ms = last_activity + SLEEP_TIMEOUT_MS - 1;
  loop();
  assert(!slept);
  now_ms = last_activity + SLEEP_TIMEOUT_MS;
  loop();
  assert(slept);
}

static void test_frames(Fastmode mode, bool second_pass) {
  TestDisplay panel;
  panel.fastmode_state = mode;
  panel.fastmode_secondpass = second_pass;
  for (int i = 0; i < 4000; i++) panel.buffer[i] = uint8_t(i);
  panel.sendImageData();
  const std::vector<uint8_t> expected = mode == OFF
    ? std::vector<uint8_t>{0x10, 0x13}
    : std::vector<uint8_t>{uint8_t(second_pass ? 0x10 : 0x13)};
  assert(panel.commands == expected);
  assert(panel.spi.transactions == int(expected.size()));
  assert(panel.spi.completed == panel.spi.transactions);
  for (const auto &frame : panel.spi.frames) {
    assert(frame.size() == 4000);
    assert(memcmp(frame.data(), panel.buffer, 4000) == 0);
  }
  assert(pin_levels[PIN_DISPLAY_CS] == HIGH);
}

static void test_power(bool initialized) {
  TestDisplay panel;
  panel.begun = initialized;
  panel.prepareSleep();
  assert(panel.spi.stopped == (initialized ? 1 : 0));
  assert(pin_levels[PIN_PCB_VEXT] == LOW);
  assert(pin_modes[PIN_PCB_VEXT] == OUTPUT);
  assert(vext_held && deep_sleep_hold);
  for (int pin : {1, 2, 3, 4, 5, 6}) assert(pin_modes[pin] == INPUT);
  panel.begin();
  assert(!vext_held && !deep_sleep_hold);
}

int main() {
  reset();
  Serial.backpressured = true;
  unsigned long start = now_ms;
  setup();
  assert(Serial.tx_timeout_ms == 0);
  assert(now_ms == start);
  assert(page == 5 && renders == 2);
  Serial.backpressured = false;
  for (bool fast : {false, true})
    for (bool previous : {false, true}) test_wake(fast, previous);
  for (bool fast : {false, true})
    for (bool previous : {false, true}) test_boundary(fast, previous);
  for (bool fast : {false, true})
    for (bool boundary : {false, true})
      for (bool advance : {false, true})
        test_sleep_lock_wake(fast, boundary, advance);
  test_awake_turn(BTN_SHORT);
  test_awake_turn(BTN_LONG);
  test_awake_turn(BTN_SELECT);
  test_awake_turn(BTN_DOWN);
  reset();
  event = BTN_UP;
  loop();
  assert(page == 4 && renders == 0);
  for (ButtonEvent button : {BTN_SHORT, BTN_LONG, BTN_MENU})
    test_activity_refresh(button, false);
  test_activity_refresh(BTN_SHORT, true);
  test_activity_refresh(BTN_LONG, true);
  reset();
  mock_wake_cause = ESP_SLEEP_WAKEUP_UNDEFINED;
  setup();
  assert(page == 4 && renders == 1 && !_sleep_after_turn_pending);
  now_ms = last_activity + SLEEP_TIMEOUT_MS;
  loop();
  assert(slept && _wake_turn_page_pending);
  test_frames(OFF, false);
  test_frames(ON, false);
  test_frames(ON, true);
  test_power(false);
  test_power(true);
  puts("Power/wake/SPI and face-button dispatch regressions passed");
}
