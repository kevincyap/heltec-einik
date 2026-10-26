#pragma once
typedef int gpio_num_t;
inline int gpio_get_level(gpio_num_t pin) { return digitalRead(pin); }
extern bool vext_held;
extern bool deep_sleep_hold;
inline void gpio_hold_en(gpio_num_t) { vext_held = true; }
inline void gpio_hold_dis(gpio_num_t) { vext_held = false; }
inline void gpio_deep_sleep_hold_en() { deep_sleep_hold = true; }
inline void gpio_deep_sleep_hold_dis() { deep_sleep_hold = false; }
