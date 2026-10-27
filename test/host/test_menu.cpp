#include <cassert>
#include <climits>
#include "mocks/diagnostics_stub.h"
#include "../../src/menu.cpp"

unsigned long now_ms;
int pin_modes[49];
int pin_levels[49];
SerialMock Serial;
LittleFSMock LittleFS;
bool vext_held;
bool deep_sleep_hold;
bool slept;
esp_sleep_wakeup_cause_t mock_wake_cause;

ButtonEvent button_poll() {
  assert(false); // The menu must never own input polling.
  return BTN_NONE;
}
unsigned long button_last_activity() {
  assert(false);
  return 0;
}
void input_enable_deep_sleep_wake() {}

static void check(std::initializer_list<ButtonEvent> input, const char *expected) {
  slept = false;
  diagnostic_records.clear();
  EinkDisplay display;
  unsigned long started = now_ms;
  assert(menu_begin(display));
  MenuResult result = {MenuAction::None, nullptr};
  for (ButtonEvent event : input) {
    assert(result.action == MenuAction::None);
    result = menu_handle_event(display, event);
  }
  assert(result.action == (expected[0] ? MenuAction::Book : MenuAction::Upload));
  if (expected[0])
    assert(strcmp(result.filename, expected) == 0);
  else
    assert(result.filename == nullptr);
  assert(now_ms == started);
  assert(!slept);
  assert(diagnostic_records.empty());
}

int main() {
  LittleFS.files = {"first.txt", "second.txt"};
  check({BTN_NONE, BTN_NONE, BTN_SELECT}, "/first.txt");
  now_ms = ULONG_MAX;
  check({BTN_NONE, BTN_NONE, BTN_SELECT}, "/first.txt");
  check({BTN_SELECT}, "/first.txt");
  check({BTN_DOWN, BTN_SELECT}, "/second.txt");
  check({BTN_DOWN, BTN_UP, BTN_SELECT}, "/first.txt");
  check({BTN_UP, BTN_SELECT}, "");
  check({BTN_UP, BTN_DOWN, BTN_SELECT}, "/first.txt");
  check({BTN_MENU, BTN_DOWN, BTN_SELECT}, "/second.txt");
  check({BTN_SHORT, BTN_LONG}, "/second.txt");
  LittleFS.files = {"book.pc", "BOOK.TxT", "book.pos"};
  check({BTN_SELECT}, "/BOOK.TxT");
  LittleFS.files.clear();
  check({BTN_UP, BTN_DOWN, BTN_SELECT}, "");
  puts("Menu up/down/select, wraparound, directory chord and empty-list tests passed");
}
