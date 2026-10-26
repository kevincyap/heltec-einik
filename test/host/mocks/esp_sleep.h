#pragma once
enum esp_sleep_wakeup_cause_t {
  ESP_SLEEP_WAKEUP_UNDEFINED,
  ESP_SLEEP_WAKEUP_EXT0,
  ESP_SLEEP_WAKEUP_EXT1
};
#define ESP_EXT1_WAKEUP_ANY_LOW 0
extern uint64_t wake_mask;
extern int ext0_wake_pin;
inline void esp_sleep_enable_ext0_wakeup(int pin, int level) {
  ext0_wake_pin = pin;
  wake_mask = level == 0 ? (1ULL << pin) : 0;
}
inline void esp_sleep_enable_ext1_wakeup(uint64_t mask, int) { wake_mask = mask; }
extern esp_sleep_wakeup_cause_t mock_wake_cause;
extern bool slept;
inline esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause() { return mock_wake_cause; }
inline void esp_deep_sleep_start() { slept = true; }
