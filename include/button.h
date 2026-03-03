#pragma once

enum ButtonEvent {
  BTN_NONE,
  BTN_SHORT,
  BTN_LONG,
  BTN_MENU
};

void button_init();
ButtonEvent button_poll();
unsigned long button_last_activity();
