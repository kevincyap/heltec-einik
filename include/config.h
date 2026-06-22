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
#define MENU_PRESS_MS     3000

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
// Vision Master E213: VBAT sense = GPIO 7, ADC enable = GPIO 46 (active-high).
// GPIO 46 drives the base of an S9013 NPN transistor. Drive HIGH to saturate
// the transistor and complete the voltage divider; LOW to cut it off.
// WARNING: Do NOT use A0 (GPIO 1) — that is the e-ink BUSY pin on E213.
#define BATTERY_INDICATOR_ENABLED  1
#define BATTERY_ADC_PIN            7      // GPIO 7 — VBAT sense
#define BATTERY_ADC_CTRL_PIN       46     // GPIO 46 — ADC_CTRL, set INPUT to enable
#define BATTERY_DIVIDER_RATIO      4.9f   // Onboard voltage divider scaling
#define BATTERY_EMPTY_MV           3300
#define BATTERY_FULL_MV            4200

// -- WiFi secrets/config --
// Keep personal WiFi credentials in `wifi_secrets.local.h` (gitignored).
#include "wifi_secrets.h"
