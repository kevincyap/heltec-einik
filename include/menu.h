#pragma once

#include "config.h"
#include "display.h"
#include "button.h"

enum class MenuAction { None, Book, Upload };

struct MenuResult {
  MenuAction action;
  // Valid for Book results until the next menu_begin().
  const char *filename;
};

// Scan and render once. Input polling and inactivity belong to the main loop.
bool menu_begin(EinkDisplay &display);
MenuResult menu_handle_event(EinkDisplay &display, ButtonEvent event);
