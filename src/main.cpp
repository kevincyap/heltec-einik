#include <Arduino.h>
#include <LittleFS.h>
#include <esp_sleep.h>
#include "config.h"
#include "display.h"
#include "button.h"
#include "reader.h"
#include "state.h"
#include "menu.h"
#include "wifi_upload.h"

EinkDisplay display;
static bool _sleep_after_turn_pending = false;
RTC_DATA_ATTR static bool _wake_turn_page_pending = false;
RTC_DATA_ATTR static bool _rtc_reader_locked = false;

static void mark_page_turned() {
#if SLEEP_AFTER_PAGE_TURN
  _sleep_after_turn_pending = true;
#endif
}

// Fast-wake data: persisted across deep sleep to avoid state_load + load_page_cache
RTC_DATA_ATTR static char     _rtc_filename[64];
RTC_DATA_ATTR static uint32_t _rtc_file_size;
RTC_DATA_ATTR static int      _rtc_num_pages;
RTC_DATA_ATTR static int      _rtc_page;
RTC_DATA_ATTR static int      _rtc_page_window[4]; // offsets: [page-1, page, page+1, page+2]

enum SleepReason {
  SLEEP_REASON_GENERIC,
  SLEEP_REASON_TIMEOUT_READING,
  SLEEP_REASON_AFTER_TURN
};

static const char *sleep_reason_name(SleepReason reason) {
  switch (reason) {
    case SLEEP_REASON_GENERIC: return "generic";
    case SLEEP_REASON_TIMEOUT_READING: return "timeout_reading";
    case SLEEP_REASON_AFTER_TURN: return "after_turn";
    default: return "unknown";
  }
}

static void render_status_screen(const char *line1, const char *line2 = nullptr) {
  display.fastmodeOff();
  display.clearMemory();
  display.fillRect(0, 0, display.width(), display.height(), WHITE);
  display.setFont(NULL);
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.setCursor(4, 18);
  display.print(line1);
  if (line2 && line2[0] != '\0') {
    display.setCursor(4, 34);
    display.print(line2);
  }
  display.update();
}

static void enter_sleep(SleepReason reason = SLEEP_REASON_GENERIC) {
  _wake_turn_page_pending = (reason == SLEEP_REASON_AFTER_TURN);
#if INPUT_SINGLE_BUTTON
  _wake_turn_page_pending = _wake_turn_page_pending ||
                           reason == SLEEP_REASON_TIMEOUT_READING;
#endif
  Serial.printf("Sleep reason=%s, set wake_flag=%d\n",
                sleep_reason_name(reason),
                _wake_turn_page_pending ? 1 : 0);

  if (reader_is_open()) {
    state_save(reader_filename(), reader_current_page());
    state_save_book_page(reader_filename(), reader_current_page());
    if (_wake_turn_page_pending) {
      strncpy(_rtc_filename, reader_filename(), sizeof(_rtc_filename) - 1);
      _rtc_filename[sizeof(_rtc_filename) - 1] = '\0';
      reader_get_page_window(&_rtc_page, &_rtc_num_pages, &_rtc_file_size, _rtc_page_window);
    }
  }

  _rtc_reader_locked = reader_is_open() && reason != SLEEP_REASON_GENERIC;
  if (_rtc_reader_locked)
    reader_render(display, true);

  Serial.println("Entering deep sleep...");
  Serial.flush();
  display.prepareSleep();
  input_enable_deep_sleep_wake();
  esp_deep_sleep_start();
}

static void enter_upload_mode() {
  reader_close();  // Free text buffer to save RAM for WiFi
  wifi_upload_start(display);
  unsigned long upload_started = millis();

  // Process DNS + wait for button press to exit
  while (wifi_upload_active()) {
    wifi_upload_tick();

    if (millis() - upload_started > UPLOAD_AUTO_EXIT_MS) {
      Serial.println("Upload mode timeout; exiting");
      wifi_upload_stop();
      break;
    }

    ButtonEvent evt = button_poll();
    if (evt != BTN_NONE) {
      wifi_upload_stop();
      break;
    }
    delay(UPLOAD_IDLE_DELAY_MS);
  }
}

static bool open_menu() {
  char selected[64];
  while (menu_show(display, selected, sizeof(selected))) {
    if (selected[0] == '\0') {
      // Upload mode requested
      enter_upload_mode();
      continue;
    }

    // Save current position before closing so it can be restored if the user
    // returns to the same book from the menu.
    if (reader_is_open()) {
      state_save(reader_filename(), reader_current_page());
      state_save_book_page(reader_filename(), reader_current_page());
    }

    // Resume from saved per-book position.
    int start_page = state_load_book_page(selected);

    reader_close();
    if (reader_open(display, selected, start_page)) {
      state_save(reader_filename(), reader_current_page());
      reader_render(display);
      return true;
    }

    render_status_screen("Failed to open book", "Returning to menu...");
    delay(500);
  }

  return false;
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  Serial.setTxTimeoutMs(0);
#endif
  Serial.println("E-Ink Reader starting...");

  esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_cause();
  bool woke_from_button = input_woke_from_button(wake_cause);
  bool restore_sleep_frame = woke_from_button && _rtc_reader_locked;
  _rtc_reader_locked = false;
  Serial.printf("Wake cause=%d, rtc wake_flag(before)=%d\n",
                (int)wake_cause,
                _wake_turn_page_pending ? 1 : 0);

  bool should_turn_page_after_wake = (woke_from_button && _wake_turn_page_pending);
  _wake_turn_page_pending = false;
  Serial.printf("Wake decision: woke_from_button=%d, auto_advance=%d, rtc wake_flag(after)=%d\n",
                woke_from_button ? 1 : 0,
                should_turn_page_after_wake ? 1 : 0,
                _wake_turn_page_pending ? 1 : 0);

  display.begin();
  display.setRotation(DISPLAY_READ_ROTATION);
  unsigned long wake_hold_ms = input_wait_wake_release();
  bool wake_prev_requested = false;
#if !INPUT_SINGLE_BUTTON
  wake_prev_requested = should_turn_page_after_wake && wake_hold_ms >= LONG_PRESS_MS;
#endif
  if (should_turn_page_after_wake) {
    Serial.printf("Wake hold=%lu ms, direction=%s\n",
                  wake_hold_ms,
                  wake_prev_requested ? "prev" : "next");
  }
  button_init();

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed!");
    render_status_screen("LittleFS mount failed", "Re-upload filesystem");
    return;
  }
  Serial.println("LittleFS mounted.");

  // Fast wake path: skip state_load + load_page_cache entirely using RTC data
  if (should_turn_page_after_wake && _rtc_filename[0] != '\0' && _rtc_num_pages > 0) {
    Serial.printf("Fast wake: %s page %d\n", _rtc_filename, _rtc_page);
    if (reader_open_fast(_rtc_filename, _rtc_file_size, _rtc_num_pages,
                         _rtc_page, _rtc_page_window)) {
      // Recreate the sleeping frame before removing its lock via a partial update.
      reader_render(display, restore_sleep_frame);
      bool turned = wake_prev_requested ? reader_prev_page() : reader_next_page();
      if (turned) {
        Serial.printf("Fast wake turn: %s -> page %d\n",
                      wake_prev_requested ? "prev" : "next",
                      reader_current_page());
      }
      if (turned || restore_sleep_frame)
        reader_render(display);
      state_save(reader_filename(), reader_current_page());
      // Reload full offset array so subsequent page turns work correctly.
      // Fast wake only populated 4 nearby offsets; all others are zeroed.
      reader_load_full_cache(display.width(), display.height());
      if (turned)
        mark_page_turned();
      Serial.println("Fast wake render done");
      return;
    }
    Serial.println("Fast wake failed; falling back to normal resume");
  }

  // Try to resume from saved state
  ReadingState saved = state_load();
  if (saved.valid && LittleFS.exists(saved.filename)) {
    Serial.printf("Resuming: %s page %d\n", saved.filename, saved.page);
    if (reader_open(display, saved.filename, saved.page)) {
      reader_render(display, restore_sleep_frame);

      bool turned = false;
      if (should_turn_page_after_wake) {
        turned = wake_prev_requested ? reader_prev_page() : reader_next_page();
        if (turned) {
          Serial.printf("Wake turn applied: %s -> page %d\n",
                        wake_prev_requested ? "prev" : "next",
                        reader_current_page());
          mark_page_turned();
        } else {
          Serial.println("Wake turn requested at boundary; staying on current page");
        }
      }
      if (turned || restore_sleep_frame)
        reader_render(display);

      state_save(reader_filename(), reader_current_page());
      Serial.println("Resume render done");
      return;
    }
    Serial.println("Resume failed; opening menu");
  }

  if (!open_menu()) {
    Serial.println("Menu failed to open");
    render_status_screen("Menu failed", "Reboot or reflash FS");
  }
}

void loop() {
  ButtonEvent evt = button_poll();

  if (reader_is_open()) {
    switch (evt) {
      case BTN_SHORT:
      case BTN_SELECT:
        if (reader_next_page()) {
          reader_render(display);
          state_save(reader_filename(), reader_current_page());
          mark_page_turned();
        }
        break;
      case BTN_LONG:
      case BTN_DOWN:
        if (reader_prev_page()) {
          reader_render(display);
          state_save(reader_filename(), reader_current_page());
          mark_page_turned();
        }
        break;
      case BTN_MENU:
        _sleep_after_turn_pending = false;
        open_menu();
        break;
      default:
        break;
    }

  }

  // Every button event, including a book-boundary press, resets inactivity.
  if (millis() - button_last_activity() >= SLEEP_TIMEOUT_MS) {
    if (reader_is_open())
      enter_sleep(_sleep_after_turn_pending ? SLEEP_REASON_AFTER_TURN
                                           : SLEEP_REASON_TIMEOUT_READING);
    else
      enter_sleep(SLEEP_REASON_GENERIC);
  }

  delay(MAIN_LOOP_IDLE_DELAY_MS);
}