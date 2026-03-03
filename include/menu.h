#pragma once

#include "config.h"
#include <heltec-eink-modules.h>

// Shows the book selection menu. Blocks until user selects a book or cancels.
// Returns true and fills `selected` with the filename if a book was chosen.
bool menu_show(DISPLAY_TYPE &display, char *selected, size_t max_len);
