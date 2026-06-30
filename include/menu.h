#pragma once

#include "config.h"
#include "display.h"

#define MENU_RESULT_UPLOAD -2

// Shows the book selection menu. Blocks until user selects a book or cancels.
// Returns true and fills `selected` with the filename if a book was chosen.
// If user selects "Upload Books", returns true with selected set to empty string.
// Caller should check: if selected[0] == '\0', enter upload mode.
bool menu_show(EinkDisplay &display, char *selected, size_t max_len);
