#pragma once

// =============================================================================
// Board selection
// -----------------------------------------------------------------------------
// Exactly one board macro is defined per PlatformIO environment (build_flags):
//   -D BOARD_VISION_MASTER_E213   Heltec Vision Master E213 (LCMEN2R13EFC1)
//   -D BOARD_CROWPANEL_E213       Elecrow CrowPanel 2.13" (SSD1680Z, DIE01021S)
// Default to the Heltec board if none is supplied.
// =============================================================================
#if !defined(BOARD_VISION_MASTER_E213) && !defined(BOARD_CROWPANEL_E213)
  #define BOARD_VISION_MASTER_E213
#endif

// =============================================================================
// Heltec Vision Master E213
// =============================================================================
#if defined(BOARD_VISION_MASTER_E213)

  // -- Display backend (heltec-eink-modules) --
  // Use EInkDisplay_VisionMasterE213 for V1.0 boards
  // Use EInkDisplay_VisionMasterE213V1_1 for V1.1 boards
  #define DISPLAY_TYPE EInkDisplay_VisionMasterE213

  // -- Input: single USER button on GPIO 21 --
  #define INPUT_SINGLE_BUTTON 1
  #define USER_BUTTON_PIN   21
  #define LONG_PRESS_MS     400
  #define MENU_PRESS_MS     3000

  // Sleep immediately after a page turn and wake to advance (fast partial wake).
  #define SLEEP_AFTER_PAGE_TURN 1

  // -- Battery indicator (reader footer) --
  // Vision Master E213: VBAT sense = GPIO 7, ADC enable = GPIO 46 (active-high).
  // GPIO 46 drives the base of an S9013 NPN transistor. Drive HIGH to saturate
  // the transistor and complete the voltage divider; LOW to cut it off.
  // WARNING: Do NOT use A0 (GPIO 1) — that is the e-ink BUSY pin on E213.
  #define BATTERY_INDICATOR_ENABLED  1
  #define BATTERY_ADC_PIN            7      // GPIO 7 — VBAT sense
  #define BATTERY_ADC_CTRL_PIN       46     // GPIO 46 — ADC_CTRL, drive HIGH to enable
  #define BATTERY_DIVIDER_RATIO      4.9f   // Onboard voltage divider scaling
  #define BATTERY_EMPTY_MV           3300
  #define BATTERY_FULL_MV            4200

// =============================================================================
// Elecrow CrowPanel 2.13" (SSD1680Z / JD79661, ESP32-S3)
// =============================================================================
#elif defined(BOARD_CROWPANEL_E213)

  // -- Display backend (GxEPD2, SSD1680 122x250) --
  // Display SPI/control pins (from Elecrow factory example):
  #define EPD_SCK   12
  #define EPD_MOSI  11
  #define EPD_RST   10
  #define EPD_DC    13
  #define EPD_CS    14
  #define EPD_BUSY  9
  #define EPD_PWR   7      // Panel power enable, drive HIGH to power the e-paper

  // -- Input: discrete buttons + rotary dial (active-low) --
  #define INPUT_SINGLE_BUTTON 0
  #define KEY_HOME  2      // Menu/Home button
  #define KEY_EXIT  1      // Back button
  #define KEY_PREV  6      // Dial: previous
  #define KEY_NEXT  4      // Dial: next
  #define KEY_OK    5      // Dial: push / select
  #define LONG_PRESS_MS     400
  #define MENU_PRESS_MS     3000

  // GxEPD2 deep-sleep wake loses the differential buffer (forces a full refresh
  // on wake), so sleeping after every page turn would flash. Rely on idle sleep.
  #define SLEEP_AFTER_PAGE_TURN 0

  // No documented battery divider on this board.
  #define BATTERY_INDICATOR_ENABLED  0

#endif

// =============================================================================
// Shared layout / timing / persistence (board-independent)
// =============================================================================

// -- Display layout --
#define LINE_HEIGHT       16
#define MARGIN_X          4
#define MARGIN_TOP        14
#define MARGIN_BOTTOM     4

// -- Button timing --
#define DEBOUNCE_MS       50

// -- Power management --
#define SLEEP_TIMEOUT_MS  (5UL * 60UL * 1000UL)
#define MAIN_LOOP_IDLE_DELAY_MS 25
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

// -- WiFi secrets/config --
// Keep personal WiFi credentials in `wifi_secrets.local.h` (gitignored).
#include "wifi_secrets.h"
