#pragma once

// -- Board variant --
// Use EInkDisplay_VisionMasterE213 for V1.0 boards
// Use EInkDisplay_VisionMasterE213V1_1 for V1.1 boards
#define DISPLAY_TYPE EInkDisplay_VisionMasterE213

// -- Display layout --
#define LINE_HEIGHT       16
#define MARGIN_X          4
#define MARGIN_TOP        14
#define MARGIN_BOTTOM     4

// -- Button (GPIO 21 = USER button on Vision Master) --
#define USER_BUTTON_PIN   21
#define DEBOUNCE_MS       50
#define LONG_PRESS_MS     600
#define MENU_PRESS_MS     2000

// -- Power management --
#define SLEEP_TIMEOUT_MS  (5UL * 60UL * 1000UL)

// -- State persistence --
#define STATE_FILE        "/state.dat"

// -- Partial refresh management --
#define FULL_REFRESH_INTERVAL 10
