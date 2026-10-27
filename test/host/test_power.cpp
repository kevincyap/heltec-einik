#include <cassert>
#include <climits>
#include "mocks/diagnostics_stub.h"
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
static bool refresh_activity_during_check;
static unsigned long input_poll_delay;
static unsigned long render_delay;
static unsigned long state_save_delay;
static unsigned long activity_check_delay;
static bool book_open;
static bool open_ok;
static bool saved_valid;
static bool menu_ok;
static MenuResult menu_result;
static bool uploading;
static int upload_ticks;
static int upload_stops;
static unsigned long upload_tick_delay;
static int input_polls;
static int saves;
static int bookmarks;
static int bookmark_page;
static int resume_page;
static std::string filename;
static std::string bookmarked_filename;

void button_init() { last_activity = now_ms; }
ButtonEvent button_poll() {
  input_polls++;
  delay(input_poll_delay);
  ButtonEvent result = event;
  event = BTN_NONE;
  if (result != BTN_NONE) last_activity = now_ms;
  return result;
}
unsigned long button_last_activity() {
  delay(activity_check_delay);
  if (refresh_activity_during_check) {
    delay(1);
    last_activity = millis();
  }
  return last_activity;
}
unsigned long button_last_event_time() { return last_activity - 123; }
void input_enable_deep_sleep_wake() {}
bool input_woke_from_button(esp_sleep_wakeup_cause_t cause) {
  return cause == ESP_SLEEP_WAKEUP_EXT0;
}
unsigned long input_wait_wake_release() { return wake_hold_ms; }
bool reader_is_open() { return book_open; }
const char *reader_filename() { return filename.c_str(); }
int reader_current_page() { return page; }
void reader_close() { book_open = false; }
bool reader_open(EinkDisplay &, const char *name, int start) {
  page = start;
  filename = name;
  book_open = open_ok;
  return open_ok;
}
bool reader_open_fast(const char *, uint32_t, int, int start, const int[4]) {
  page = start;
  book_open = fast_open_ok;
  return fast_open_ok;
}
void reader_render(EinkDisplay &, bool sleeping) {
  delay(render_delay);
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
  result.valid = saved_valid;
  return result;
}
bool state_save(const char *, int) { saves++; delay(state_save_delay); return true; }
int state_load_book_page(const char *) { return resume_page; }
bool state_save_book_page(const char *name, int saved_page) {
  bookmarks++;
  bookmark_page = saved_page;
  bookmarked_filename = name;
  return true;
}
bool menu_begin(EinkDisplay &) { return menu_ok; }
MenuResult menu_handle_event(EinkDisplay &, ButtonEvent) {
  MenuResult result = menu_result;
  menu_result = {MenuAction::None, nullptr};
  return result;
}
void wifi_upload_start(EinkDisplay &) { uploading = true; }
void wifi_upload_tick() { upload_ticks++; delay(upload_tick_delay); }
void wifi_upload_stop() { uploading = false; upload_stops++; }
bool wifi_upload_active() { return uploading; }

struct TestDisplay : EinkDisplay {
  using LCMEN2R13EFC1::begun;
  using LCMEN2R13EFC1::buffer;
  using LCMEN2R13EFC1::commands;
  using LCMEN2R13EFC1::fastmode_secondpass;
  using LCMEN2R13EFC1::fastmode_state;
  using LCMEN2R13EFC1::spi;
  using EinkDisplay::sendImageData;
  using EinkDisplay::wait;
};

static void reset() {
  LittleFS.files = {"book.txt"};
  now_ms = 1000;
  last_activity = now_ms;
  refresh_activity_during_check = false;
  diagnostic_poll_delay = input_poll_delay = render_delay = 0;
  state_save_delay = activity_check_delay = 0;
  book_open = open_ok = saved_valid = menu_ok = true;
  filename = "/book.txt";
  bookmarked_filename.clear();
  menu_result = {MenuAction::None, nullptr};
  uploading = false;
  upload_ticks = upload_stops = input_polls = saves = bookmarks = 0;
  bookmark_page = -1;
  resume_page = 2;
  upload_tick_delay = 0;
  set_mode(AppMode::Reading);
  page = 4;
  renders = cache_loads = 0;
  sleep_frames.clear();
  diagnostic_records.clear();
  diagnostic_flushes = 0;
  diagnostic_exporting = false;
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

static void test_activity_clock_tick(bool rollover) {
  reset();
  if (rollover) {
    now_ms = ULONG_MAX;
    set_mode(AppMode::Reading);
  }
  refresh_activity_during_check = true;
  for (ButtonEvent button : {BTN_SELECT, BTN_SELECT, BTN_NONE}) {
    event = button;
    loop();
    assert(!slept);
  }
  assert(page == 6);
  refresh_activity_during_check = false;
  now_ms = last_activity + SLEEP_TIMEOUT_MS - 1;
  loop();
  assert(!slept);
  now_ms = last_activity + SLEEP_TIMEOUT_MS;
  loop();
  assert(slept);
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

static void test_display_deadline() {
  reset();
  TestDisplay panel;
  pin_levels[PIN_DISPLAY_BUSY] = LOW;
  unsigned long started = now_ms;
  try {
    panel.sendImageData();
    assert(false);
  } catch (const DisplayTimeout &error) {
    assert(error.pin == PIN_DISPLAY_BUSY);
    assert(error.elapsed == DISPLAY_BUSY_TIMEOUT_MS);
  }
  assert(now_ms - started == DISPLAY_BUSY_TIMEOUT_MS);
  assert(panel.commands.empty() && panel.spi.transactions == 0);
  now_ms = ULONG_MAX - 10;
  try {
    panel.wait();
    assert(false);
  } catch (const DisplayTimeout &error) {
    assert(error.elapsed == DISPLAY_BUSY_TIMEOUT_MS);
  }
  pin_levels[PIN_DISPLAY_BUSY] = HIGH;
  panel.wait();
  try {
    display_wait_busy(PIN_DISPLAY_BUSY, HIGH);
    assert(false);
  } catch (const DisplayTimeout &error) {
    assert(error.elapsed == DISPLAY_BUSY_TIMEOUT_MS);
  }
  reset();
}

static void test_loop_timing(unsigned long diag, unsigned long input,
                             unsigned long render, unsigned long state,
                             unsigned long idle, bool rollover = false) {
  reset();
  if (rollover) now_ms = last_activity = ULONG_MAX - 1000;
  diagnostic_poll_delay = diag;
  input_poll_delay = input;
  render_delay = render;
  state_save_delay = state;
  activity_check_delay = idle;
  event = render || state ? BTN_SELECT : BTN_NONE;
  ButtonEvent dispatched = event;
  unsigned long total = diag + input + render + state + idle;
  loop();
  unsigned warnings = 0;
  for (const auto &record : diagnostic_records) {
    if (record.text.find("LOOP_SLOW ") != 0) continue;
    char expected[192];
    snprintf(expected, sizeof(expected),
             "LOOP_SLOW event=%d page=%d diag=%lu input=%lu action=%lu idle=%lu total=%lu mode=reading",
             (int)dispatched, page + 1, diag, input, render + state, idle, total);
    assert(record.text == expected);
    warnings++;
  }
  assert(warnings == (total >= MAIN_LOOP_SLOW_MS ? 1U : 0U));
  assert(diagnostic_flushes == 0 && !slept);
}

static void show_menu() {
  event = BTN_MENU;
  int old_polls = input_polls;
  loop();
  assert(_mode == AppMode::Menu && input_polls == old_polls + 1);
  assert(!_sleep_after_turn_pending);
}

static void choose_upload() {
  show_menu();
  menu_result = {MenuAction::Upload, nullptr};
  event = BTN_SELECT;
  loop();
  assert(_mode == AppMode::Upload && uploading && !book_open);
  assert(bookmarks == 1 && bookmark_page == 4);
  assert(bookmarked_filename == "/book.txt");
}

static void test_menu_modes() {
  reset();
  _sleep_after_turn_pending = true;
  show_menu();
  assert(book_open && bookmarks == 0);
  menu_result = {MenuAction::Book, "/other.txt"};
  event = BTN_SELECT;
  loop();
  assert(_mode == AppMode::Reading && filename == "/other.txt");
  assert(page == resume_page && renders == 1 && saves == 2);
  assert(bookmarks == 1 && bookmark_page == 4);
  assert(bookmarked_filename == "/book.txt");
  assert(!_sleep_after_turn_pending);

  reset();
  show_menu();
  now_ms = _mode_started + SLEEP_TIMEOUT_MS - 1;
  loop();
  assert(!slept);
  now_ms = _mode_started + SLEEP_TIMEOUT_MS;
  loop();
  assert(slept && !_wake_turn_page_pending && !_rtc_reader_locked);
  assert(bookmarks == 1 && sleep_frames.empty());
}

static void test_upload_modes(bool rollover) {
  reset();
  if (rollover) now_ms = last_activity = ULONG_MAX - 1000;
  choose_upload();
  unsigned long upload_started = _mode_started;
  now_ms = upload_started + SLEEP_TIMEOUT_MS;
  int old_polls = input_polls;
  diagnostic_poll_delay = 7;
  input_poll_delay = 3;
  loop();
  assert(now_ms == upload_started + SLEEP_TIMEOUT_MS + 10 + UPLOAD_IDLE_DELAY_MS);
  assert(input_polls == old_polls + 1 && upload_ticks == 1);
  assert(_mode == AppMode::Upload && !slept);
  diagnostic_poll_delay = input_poll_delay = 0;
  now_ms = upload_started + UPLOAD_AUTO_EXIT_MS;
  loop();
  assert(_mode == AppMode::Upload && !slept);
  now_ms = upload_started + UPLOAD_AUTO_EXIT_MS + 1;
  loop();
  assert(_mode == AppMode::Menu && upload_stops == 1 && !slept);
  assert(bookmarks == 1); // No attempt to save the closed reader on upload exit.
  now_ms = _mode_started + SLEEP_TIMEOUT_MS - 1;
  loop();
  assert(!slept);
  now_ms = _mode_started + SLEEP_TIMEOUT_MS;
  loop();
  assert(slept && !_wake_turn_page_pending);

  for (bool inactive : {false, true}) {
    reset();
    choose_upload();
    if (inactive) uploading = false;
    else event = BTN_UP;
    loop();
    assert(_mode == AppMode::Menu && !uploading && upload_stops == 1);
    assert(!slept && page == 4);
  }
}

static void test_error_modes() {
  reset();
  show_menu();
  open_ok = false;
  menu_result = {MenuAction::Book, "/missing.txt"};
  event = BTN_SELECT;
  unsigned long started = now_ms;
  loop();
  assert(_mode == AppMode::BookError);
  assert(now_ms - started == MAIN_LOOP_IDLE_DELAY_MS);
  assert(bookmarks == 1 && bookmark_page == 4);
  now_ms = _mode_started + 499;
  loop();
  assert(_mode == AppMode::BookError);
  now_ms = _mode_started + 500;
  loop();
  assert(_mode == AppMode::Menu);

  reset();
  menu_ok = false;
  event = BTN_MENU;
  loop();
  assert(_mode == AppMode::Fatal);
  int old_page = page;
  event = BTN_SELECT;
  loop();
  assert(_mode == AppMode::Fatal && page == old_page);

  reset();
  _wake_turn_page_pending = false;
  saved_valid = false;
  book_open = false;
  setup();
  assert(_mode == AppMode::Menu && input_polls == 0);
  event = BTN_NONE;
  loop();
  assert(input_polls == 1 && !slept);
}

static void test_mode_timing(AppMode mode) {
  reset();
  set_mode(mode);
  uploading = mode == AppMode::Upload;
  book_open = false;
  diagnostic_poll_delay = 10;
  input_poll_delay = 20;
  activity_check_delay = MAIN_LOOP_SLOW_MS;
  if (uploading) upload_tick_delay = 30;
  unsigned long started = now_ms;
  loop();
  assert(input_polls == 1 && !slept);
  assert(upload_ticks == (uploading ? 1 : 0));
  assert(now_ms - started == 30 + (uploading ? 30 : 0) +
         2 * MAIN_LOOP_SLOW_MS +
         (uploading ? UPLOAD_IDLE_DELAY_MS : MAIN_LOOP_IDLE_DELAY_MS));
  assert(diagnostic_records.size() == 1);
  char expected[256];
  snprintf(expected, sizeof(expected),
           "LOOP_SLOW event=0 page=0 diag=10 input=20 action=%d idle=%lu total=%lu mode=%s",
           uploading ? 30 : 0, (unsigned long)MAIN_LOOP_SLOW_MS,
           30UL + (uploading ? 30 : 0) + MAIN_LOOP_SLOW_MS, mode_name(mode));
  assert(diagnostic_records[0].text == expected);
}

static void test_export_service() {
  for (AppMode mode : {AppMode::Reading, AppMode::Menu, AppMode::Upload,
                       AppMode::BookError, AppMode::Fatal}) {
    reset();
    set_mode(mode);
    uploading = mode == AppMode::Upload;
    diagnostic_exporting = true;
    now_ms += SLEEP_TIMEOUT_MS;
    unsigned long started = now_ms;
    loop();
    assert(!slept && input_polls == 1 && now_ms == started + 1);
    diagnostic_exporting = false;
    if (_mode != AppMode::Upload && _mode != AppMode::Menu) {
      loop();
      assert(slept);
    }
  }
  reset();
  diagnostic_exporting = true;
  for (ButtonEvent button : {BTN_SELECT, BTN_DOWN, BTN_SELECT}) {
    event = button;
    unsigned long started = now_ms;
    loop();
    assert(!slept && now_ms == started + 1);
  }
  assert(page == 5 && renders == 3 && saves == 3);
  event = BTN_UP;
  loop();
  assert(diagnostic_records.back().text == "BUG_MARKER page=6");
  assert(diagnostic_flushes == 1);
  assert(Serial.output.find("persisted=0") != std::string::npos);
  event = BTN_MENU;
  loop();
  assert(_mode == AppMode::Menu && !slept);
}

int main() {
  test_export_service();
  test_menu_modes();
  test_upload_modes(false);
  test_upload_modes(true);
  test_error_modes();
  for (AppMode mode : {AppMode::Menu, AppMode::Upload, AppMode::BookError, AppMode::Fatal})
    test_mode_timing(mode);
  test_loop_timing(0, 0, MAIN_LOOP_SLOW_MS - 1, 0, 0);
  test_loop_timing(MAIN_LOOP_SLOW_MS, 0, 0, 0, 0);
  test_loop_timing(0, MAIN_LOOP_SLOW_MS, 0, 0, 0);
  test_loop_timing(0, 0, MAIN_LOOP_SLOW_MS, 0, 0);
  test_loop_timing(0, 0, 0, MAIN_LOOP_SLOW_MS, 0);
  test_loop_timing(0, 0, 0, 0, MAIN_LOOP_SLOW_MS);
  test_loop_timing(250, 10, 1500, 240, 0, true);
  test_loop_timing(0, 0, 30000, 0, 0);
  test_display_deadline();
  reset();
  Serial.backpressured = true;
  unsigned long start = now_ms;
  setup();
  assert(Serial.tx_timeout_ms == 1);
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
  unsigned long marker_time = last_activity - 123;
  loop();
  assert(page == 4 && renders == 0);
  assert(diagnostic_records.size() == 1);
  assert(diagnostic_records[0].text == "BUG_MARKER page=5");
  assert(diagnostic_records[0].when == marker_time && diagnostic_flushes == 1);
  for (ButtonEvent button : {BTN_SHORT, BTN_LONG, BTN_MENU})
    test_activity_refresh(button, false);
  test_activity_refresh(BTN_SHORT, true);
  test_activity_refresh(BTN_LONG, true);
  test_activity_clock_tick(false);
  test_activity_clock_tick(true);
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
