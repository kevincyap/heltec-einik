#include <cassert>
#define ARDUINO_ARCH_ESP32
#include "../../src/battery.cpp"

unsigned long now_ms;
int pin_modes[49];
int pin_levels[49];
SerialMock Serial;
static uint32_t readings[17];
static int read_count;
static int attenuation;

void analogSetPinAttenuation(int pin, int value) {
  assert(pin == BATTERY_ADC_PIN);
  attenuation = value;
}
uint32_t analogReadMilliVolts(int pin) {
  assert(pin == BATTERY_ADC_PIN);
  assert(pin_levels[BATTERY_ADC_CTRL_PIN] == HIGH);
  assert(read_count < 17);
  return readings[read_count++];
}

static void reset(uint32_t mv) {
  now_ms = 0;
  read_count = 0;
  _sampled = false;
  _cached_percent = -1;
  for (uint32_t &reading : readings) reading = mv;
}

int main() {
  assert(percent_from_mv(3000) == 0);
  assert(percent_from_mv(3300) == 0);
  assert(percent_from_mv(3850) == 50);
  assert(percent_from_mv(3875) == 55);
  assert(percent_from_mv(4200) == 100);
  assert(percent_from_mv(4500) == 100);
  int previous = 0;
  for (int mv = 3000; mv <= 4500; mv++) {
    int pct = percent_from_mv(float(mv));
    assert(pct >= previous && pct >= 0 && pct <= 100);
    previous = pct;
  }
  reset(800);
  readings[0] = 0; // discarded conversion must not affect the result
  assert(battery_percent() == percent_from_mv(800 * BATTERY_DIVIDER_RATIO));
  assert(read_count == 17 && attenuation == ADC_0db);
  assert(battery_voltage_mv() == 800 * BATTERY_DIVIDER_RATIO);
  assert(read_count == 17);
  assert(pin_levels[BATTERY_ADC_CTRL_PIN] == LOW);
  assert(now_ms == 21);
  int cached = battery_percent();
  assert(read_count == 17);
  now_ms = _last_sample_ms + 29999;
  assert(battery_percent() == cached && read_count == 17);
  now_ms = _last_sample_ms + 30000;
  read_count = 0;
  assert(battery_percent() == cached && read_count == 17);

  reset(800);
  readings[1] = readings[2] = 0;
  readings[3] = readings[4] = 2000;
  assert(battery_percent() == cached);
  reset(0);
  assert(battery_voltage_mv() == 0);
  assert(battery_percent() == -1);
  assert(pin_levels[BATTERY_ADC_CTRL_PIN] == LOW);
  now_ms = _last_sample_ms + 9999;
  assert(battery_percent() == -1 && read_count == 17);
  now_ms = _last_sample_ms + 10000;
  read_count = 0;
  for (uint32_t &reading : readings) reading = 800;
  assert(battery_percent() == cached && read_count == 17);
  reset(1000);
  assert(battery_voltage_mv() == 1000 * BATTERY_DIVIDER_RATIO);
  assert(battery_percent() == -1);
  assert(pin_levels[BATTERY_ADC_CTRL_PIN] == LOW);
  puts("Battery curve, startup, outlier filtering, caching and recovery tests passed");
}
