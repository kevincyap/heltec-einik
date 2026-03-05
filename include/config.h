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
#define LONG_PRESS_MS     400
#define MENU_PRESS_MS     2000

// -- Power management --
#define SLEEP_TIMEOUT_MS  (5UL * 60UL * 1000UL)
#define MAIN_LOOP_IDLE_DELAY_MS 25
#define SLEEP_AFTER_PAGE_TURN 1
#define SLEEP_AFTER_PAGE_TURN_DELAY_MS 5000
#define WAKE_BUTTON_RELEASE_TIMEOUT_MS 3000

// Auto-exit upload mode to avoid leaving WiFi active accidentally
#define UPLOAD_AUTO_EXIT_MS (10UL * 60UL * 1000UL)
#define UPLOAD_IDLE_DELAY_MS 10

// -- State persistence --
#define STATE_FILE        "/state.dat"

// -- Partial refresh management --
// Set to 0 to disable full-refresh flashes during reading.
// Set to a positive value (e.g. 20-50) if you want occasional full cleanup.
#define FULL_REFRESH_INTERVAL 25
#define PARTIAL_CLEANUP_EXTRA_UPDATES 1

// -- Battery indicator (reader footer) --
// E213 / T190: battery voltage on GPIO 37.
// WARNING: Do NOT use A0 (GPIO 1) — that is the e-ink BUSY pin on E213.
#define BATTERY_INDICATOR_ENABLED  0
#define BATTERY_ADC_PIN            37    // GPIO 37 — free ADC pin on E213/T190
#define BATTERY_DIVIDER_RATIO      2.0f  // Adjust if your hardware differs
#define BATTERY_EMPTY_MV           3300
#define BATTERY_FULL_MV            4200

// -- WiFi secrets/config --
// Keep personal WiFi credentials in `wifi_secrets.local.h` (gitignored).
#include "wifi_secrets.h"
