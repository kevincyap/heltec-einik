#pragma once
enum esp_sleep_wakeup_cause_t {
  ESP_SLEEP_WAKEUP_UNDEFINED,
  ESP_SLEEP_WAKEUP_EXT0
};
extern esp_sleep_wakeup_cause_t mock_wake_cause;
extern bool slept;
inline esp_sleep_wakeup_cause_t esp_sleep_get_wakeup_cause() { return mock_wake_cause; }
inline void esp_deep_sleep_start() { slept = true; }
