#pragma once

#include <esp_sleep.h>

enum ButtonEvent {
  BTN_NONE,
  BTN_SHORT,
  BTN_LONG,
  BTN_MENU
};

void button_init();
ButtonEvent button_poll();
unsigned long button_last_activity();
void button_inject_short_press();

// Board-abstracted deep-sleep wake helpers.
// Configure the GPIO(s) that wake the device from deep sleep.
void input_enable_deep_sleep_wake();
// True if the given wake cause corresponds to a button/key press for this board.
bool input_woke_from_button(esp_sleep_wakeup_cause_t cause);
// Block until the wake key is released (or timeout). Returns hold duration in ms.
unsigned long input_wait_wake_release();
