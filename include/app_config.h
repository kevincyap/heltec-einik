#pragma once

// -- Display layout --
#define LINE_HEIGHT       16
#define MARGIN_X          4
#define MARGIN_TOP        14
#define MARGIN_BOTTOM     4

// -- Button timing --
#define DEBOUNCE_MS       50
#define LONG_PRESS_MS     400
#define MENU_PRESS_MS     3000

// -- Power management --
#define SLEEP_TIMEOUT_MS  (60UL * 1000UL)
#define MAIN_LOOP_IDLE_DELAY_MS 25
#define MAIN_LOOP_SLOW_MS 2000UL
#define WAKE_BUTTON_RELEASE_TIMEOUT_MS 3000
#define DISPLAY_BUSY_TIMEOUT_MS 15000UL

// Auto-exit upload mode to avoid leaving WiFi active accidentally.
#define UPLOAD_AUTO_EXIT_MS (10UL * 60UL * 1000UL)
#define UPLOAD_IDLE_DELAY_MS 10

// -- State persistence --
#define STATE_FILE        "/state.dat"

// Additional partial updates after rendering; 0 disables ghosting cleanup.
#define PARTIAL_CLEANUP_EXTRA_UPDATES 0
