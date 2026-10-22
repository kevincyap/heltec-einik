#include <Arduino.h>
#include "config.h"
#include "battery.h"

#if BATTERY_INDICATOR_ENABLED && defined(ARDUINO_ARCH_ESP32)
static int _cached_percent = -1;
static bool _sampled = false;
static unsigned long _last_sample_ms = 0;
static unsigned long _sample_interval_ms = 0;

static int percent_from_mv(float mv) {
  // Approximate resting-voltage curve for a conventional 4.2 V Li-ion cell,
  // not a fuel gauge. Charging, load, temperature and cell chemistry affect it.
  static const struct {
    uint16_t mv;
    uint8_t percent;
  } curve[] = {
    {BATTERY_EMPTY_MV, 0}, {3500, 5}, {3600, 10}, {3700, 20},
    {3750, 30}, {3800, 40}, {3850, 50}, {3900, 60},
    {3950, 70}, {4000, 80}, {4100, 90}, {BATTERY_FULL_MV, 100}
  };
  if (mv <= curve[0].mv) return 0;
  for (unsigned i = 1; i < sizeof(curve) / sizeof(curve[0]); i++) {
    if (mv <= curve[i].mv) {
      float fraction = (mv - curve[i - 1].mv) / (curve[i].mv - curve[i - 1].mv);
      return (int)(curve[i - 1].percent +
                   fraction * (curve[i].percent - curve[i - 1].percent) + 0.5f);
    }
  }
  return 100;
}
#endif

int battery_percent() {
#if BATTERY_INDICATOR_ENABLED && defined(ARDUINO_ARCH_ESP32)
  unsigned long now = millis();
  if (_sampled && now - _last_sample_ms < _sample_interval_ms)
    return _cached_percent;

  pinMode(BATTERY_ADC_CTRL_PIN, OUTPUT);
  digitalWrite(BATTERY_ADC_CTRL_PIN, HIGH);
  delay(5);
  // The configured divider puts a full cell near 857 mV, within the S3's
  // 0 dB range. Lower attenuation uses more of the ADC's measurement range.
  analogSetPinAttenuation(BATTERY_ADC_PIN, ADC_0db);
  analogReadMilliVolts(BATTERY_ADC_PIN); // discard the first conversion
  uint32_t samples[16];
  for (unsigned i = 0; i < 16; i++) {
    uint32_t value = analogReadMilliVolts(BATTERY_ADC_PIN);
    unsigned j = i;
    while (j > 0 && samples[j - 1] > value) {
      samples[j] = samples[j - 1];
      j--;
    }
    samples[j] = value;
    delay(1);
  }
  digitalWrite(BATTERY_ADC_CTRL_PIN, LOW);

  uint32_t sum = 0;
  for (unsigned i = 2; i < 14; i++) sum += samples[i];
  float battery_mv = (sum / 12.0f) * BATTERY_DIVIDER_RATIO;
  bool plausible = battery_mv >= BATTERY_EMPTY_MV - 200 &&
                   battery_mv <= BATTERY_FULL_MV + 300;
  _cached_percent = plausible ? percent_from_mv(battery_mv) : -1;
  _sampled = true;
  _last_sample_ms = millis();
  _sample_interval_ms = plausible ? 30000 : 10000;
  Serial.printf("[BAT] filtered=%.1f mV, battery=%.1f mV, estimate=%d%%\n",
                sum / 12.0f, battery_mv, _cached_percent);
  return _cached_percent;
#else
  return -1;
#endif
}
