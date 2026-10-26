#include <cassert>
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

static std::vector<ButtonEvent> events;
static size_t event_index;

ButtonEvent button_poll() {
  assert(event_index < events.size());
  return events[event_index++];
}
unsigned long button_last_activity() { return now_ms; }
void input_enable_deep_sleep_wake() {}

static void check(std::initializer_list<ButtonEvent> input, const char *expected) {
  events = input;
  event_index = 0;
  EinkDisplay display;
  char selected[64] = {};
  assert(menu_show(display, selected, sizeof(selected)));
  assert(strcmp(selected, expected) == 0);
  assert(event_index == events.size());
}

int main() {
  LittleFS.files = {"first.txt", "second.txt"};
  check({BTN_SELECT}, "/first.txt");
  check({BTN_DOWN, BTN_SELECT}, "/second.txt");
  check({BTN_DOWN, BTN_UP, BTN_SELECT}, "/first.txt");
  check({BTN_UP, BTN_SELECT}, "");
  check({BTN_UP, BTN_DOWN, BTN_SELECT}, "/first.txt");
  check({BTN_MENU, BTN_DOWN, BTN_SELECT}, "/second.txt");
  check({BTN_SHORT, BTN_LONG}, "/second.txt");
  LittleFS.files.clear();
  check({BTN_UP, BTN_DOWN, BTN_SELECT}, "");
  puts("Menu up/down/select, wraparound, directory chord and empty-list tests passed");
}
