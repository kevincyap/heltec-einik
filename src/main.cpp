#include <Arduino.h>
#include "serial_output.h"
#include <LittleFS.h>
#include <esp_sleep.h>
#include <esp_system.h>
#include "config.h"
#include "display.h"
#include "button.h"
#include "reader.h"
#include "state.h"
#include "menu.h"
#include "wifi_upload.h"
#include "diagnostics.h"

EinkDisplay display;
static bool _sleep_after_turn_pending = false;
RTC_DATA_ATTR static bool _wake_turn_page_pending = false;
RTC_DATA_ATTR static bool _rtc_reader_locked = false;

enum class AppMode { Reading, Menu, Upload, BookError, Fatal };
static AppMode _mode = AppMode::Fatal;
static unsigned long _mode_started;

static void set_mode(AppMode mode) {
  _mode = mode;
  _mode_started = millis();
}

static const char *mode_name(AppMode mode) {
  switch (mode) {
    case AppMode::Reading: return "reading";
    case AppMode::Menu: return "menu";
    case AppMode::Upload: return "upload";
    case AppMode::BookError: return "book_error";
    default: return "fatal";
  }
}

static void mark_page_turned() {
#if SLEEP_AFTER_PAGE_TURN
  _sleep_after_turn_pending = true;
#endif
}

static void save_reading_state(bool bookmark = false) {
  unsigned long started = millis();
  diagnostics_log("STATE_BEGIN page=%d bookmark=%d", reader_current_page() + 1,
                  bookmark ? 1 : 0);
  state_save(reader_filename(), reader_current_page());
  if (bookmark)
    state_save_book_page(reader_filename(), reader_current_page());
  diagnostics_log("STATE_END page=%d elapsed=%lu", reader_current_page() + 1,
                  millis() - started);
}

static void render_resumed_book(bool turn_page, bool previous, bool sleeping) {
  // Match the retained panel image before performing a differential update.
  reader_render(display, sleeping);
  bool turned = false;
  if (turn_page) {
    turned = previous ? reader_prev_page() : reader_next_page();
    if (turned) {
      debug_serial.printf("Wake turn applied: %s -> page %d\n",
                    previous ? "prev" : "next", reader_current_page());
    } else {
      debug_serial.println("Wake turn requested at boundary; staying on current page");
    }
  }
  if (turned || sleeping)
    reader_render(display);
  save_reading_state();
  if (turned)
    mark_page_turned();
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
  debug_serial.printf("Sleep reason=%s, set wake_flag=%d\n",
                sleep_reason_name(reason),
                _wake_turn_page_pending ? 1 : 0);

  if (reader_is_open()) {
    save_reading_state(true);
    if (_wake_turn_page_pending) {
      strncpy(_rtc_filename, reader_filename(), sizeof(_rtc_filename) - 1);
      _rtc_filename[sizeof(_rtc_filename) - 1] = '\0';
      reader_get_page_window(&_rtc_page, &_rtc_num_pages, &_rtc_file_size, _rtc_page_window);
    }
  }

  _rtc_reader_locked = reader_is_open() && reason != SLEEP_REASON_GENERIC;
  if (_rtc_reader_locked)
    reader_render(display, true);

  diagnostics_log("SLEEP reason=%s page=%d", sleep_reason_name(reason),
                  reader_is_open() ? reader_current_page() + 1 : 0);
  diagnostics_flush();
  debug_serial.println("Entering deep sleep...");
  display.prepareSleep();
  input_enable_deep_sleep_wake();
  esp_deep_sleep_start();
}

static void enter_upload_mode() {
  if (reader_is_open())
    save_reading_state(true);
  reader_close();  // Free the page index to save RAM for WiFi.
  wifi_upload_start(display);
  set_mode(AppMode::Upload);
}

static void open_menu() {
  _sleep_after_turn_pending = false;
  if (menu_begin(display)) {
    set_mode(AppMode::Menu);
  } else {
    debug_serial.println("Menu failed to open");
    render_status_screen("Menu failed", "Reboot or reflash FS");
    set_mode(AppMode::Fatal);
  }
}

static void handle_menu_event(ButtonEvent event) {
  MenuResult result = menu_handle_event(display, event);
  if (result.action == MenuAction::Upload) {
    enter_upload_mode();
  } else if (result.action == MenuAction::Book) {
    if (reader_is_open())
      save_reading_state(true);
    int start_page = state_load_book_page(result.filename);
    if (reader_open(display, result.filename, start_page)) {
      save_reading_state();
      reader_render(display);
      set_mode(AppMode::Reading);
    } else {
      render_status_screen("Failed to open book", "Returning to menu...");
      set_mode(AppMode::BookError);
    }
  }
}

void setup() {
  set_mode(AppMode::Fatal);
  serial_output_begin();
  debug_serial.println("E-Ink Reader starting...");

  esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_cause();
  bool woke_from_button = input_woke_from_button(wake_cause);
  bool restore_sleep_frame = woke_from_button && _rtc_reader_locked;
  _rtc_reader_locked = false;
  debug_serial.printf("Wake cause=%d, rtc wake_flag(before)=%d\n",
                (int)wake_cause,
                _wake_turn_page_pending ? 1 : 0);

  bool should_turn_page_after_wake = (woke_from_button && _wake_turn_page_pending);
  _wake_turn_page_pending = false;
  debug_serial.printf("Wake decision: woke_from_button=%d, auto_advance=%d, rtc wake_flag(after)=%d\n",
                woke_from_button ? 1 : 0,
                should_turn_page_after_wake ? 1 : 0,
                _wake_turn_page_pending ? 1 : 0);

  bool fs_mounted = LittleFS.begin(true);
  if (fs_mounted)
    diagnostics_begin(esp_random(), (int)esp_reset_reason(), (int)wake_cause);

  display.begin();
  display.setRotation(DISPLAY_READ_ROTATION);
  unsigned long wake_hold_ms = input_wait_wake_release();
  bool wake_prev_requested = false;
#if !INPUT_SINGLE_BUTTON
  wake_prev_requested = should_turn_page_after_wake && wake_hold_ms >= LONG_PRESS_MS;
#endif
  if (should_turn_page_after_wake) {
    debug_serial.printf("Wake hold=%lu ms, direction=%s\n",
                  wake_hold_ms,
                  wake_prev_requested ? "prev" : "next");
  }
  button_init();

  if (!fs_mounted) {
    debug_serial.println("LittleFS mount failed!");
    render_status_screen("LittleFS mount failed", "Re-upload filesystem");
    return;
  }
  debug_serial.println("LittleFS mounted.");

  // Fast wake path: skip state_load + load_page_cache entirely using RTC data
  if (should_turn_page_after_wake && _rtc_filename[0] != '\0' && _rtc_num_pages > 0) {
    debug_serial.printf("Fast wake: %s page %d\n", _rtc_filename, _rtc_page);
    if (reader_open_fast(_rtc_filename, _rtc_file_size, _rtc_num_pages,
                         _rtc_page, _rtc_page_window)) {
      render_resumed_book(true, wake_prev_requested, restore_sleep_frame);
      // Reload full offset array so subsequent page turns work correctly.
      // Fast wake only populated 4 nearby offsets; all others are zeroed.
      reader_load_full_cache(display.width(), display.height());
      debug_serial.println("Fast wake render done");
      set_mode(AppMode::Reading);
      return;
    }
    debug_serial.println("Fast wake failed; falling back to normal resume");
  }

  // Try to resume from saved state
  ReadingState saved = state_load();
  if (saved.valid && LittleFS.exists(saved.filename)) {
    debug_serial.printf("Resuming: %s page %d\n", saved.filename, saved.page);
    if (reader_open(display, saved.filename, saved.page)) {
      render_resumed_book(should_turn_page_after_wake, wake_prev_requested,
                          restore_sleep_frame);
      debug_serial.println("Resume render done");
      set_mode(AppMode::Reading);
      return;
    }
    debug_serial.println("Resume failed; opening menu");
  }

  open_menu();
}

void loop() {
  unsigned long started = millis();
  AppMode serviced_mode = _mode;
  diagnostics_poll();
  unsigned long diagnostics_done = millis();
  ButtonEvent evt = button_poll();
  unsigned long input_done = millis();

  if (_mode == AppMode::Reading) {
    switch (evt) {
      case BTN_SHORT:
      case BTN_SELECT:
        if (reader_next_page()) {
          reader_render(display);
          save_reading_state();
          mark_page_turned();
        }
        break;
      case BTN_LONG:
      case BTN_DOWN:
        if (reader_prev_page()) {
          reader_render(display);
          save_reading_state();
          mark_page_turned();
        }
        break;
      case BTN_MENU:
        open_menu();
        break;
      case BTN_UP: {
        unsigned long pressed = button_last_event_time();
        bool recorded = diagnostics_log_at(pressed, "BUG_MARKER page=%d",
                                           reader_current_page() + 1);
        bool persisted = recorded && diagnostics_flush();
        debug_serial.printf("[DIAG] BUG_MARKER ms=%lu page=%d persisted=%d\n",
                      pressed, reader_current_page() + 1, persisted ? 1 : 0);
        break;
      }
      default:
        break;
    }
  } else if (_mode == AppMode::Menu) {
    handle_menu_event(evt);
  } else if (_mode == AppMode::Upload) {
    wifi_upload_tick();
    bool timed_out = millis() - _mode_started > UPLOAD_AUTO_EXIT_MS;
    if (timed_out)
      debug_serial.println("Upload mode timeout; exiting");
    if (!wifi_upload_active() || timed_out || evt != BTN_NONE) {
      wifi_upload_stop();
      open_menu();
    }
  } else if (_mode == AppMode::BookError &&
             millis() - _mode_started >= 500) {
    open_menu();
  }

  unsigned long action_done = millis();
  // Every button event, including a book-boundary press, resets inactivity.
  // Sample activity first: checking held buttons can advance its timestamp.
  unsigned long last_activity = button_last_activity();
  unsigned long idle_checked = millis();
  if (idle_checked - started >= MAIN_LOOP_SLOW_MS) {
    diagnostics_log("LOOP_SLOW event=%d page=%d diag=%lu input=%lu action=%lu idle=%lu total=%lu mode=%s",
                    (int)evt, reader_is_open() ? reader_current_page() + 1 : 0,
                    diagnostics_done - started, input_done - diagnostics_done,
                    action_done - input_done, idle_checked - action_done,
                    idle_checked - started, mode_name(serviced_mode));
    // Recording a warning can flush a full log buffer; recheck recent input.
    last_activity = button_last_activity();
  }
  // Starting a new mode grants a fresh inactivity window (notably after upload).
  if (!diagnostics_export_active() && _mode != AppMode::Upload &&
      millis() - _mode_started >= SLEEP_TIMEOUT_MS &&
      millis() - last_activity >= SLEEP_TIMEOUT_MS) {
    if (_mode == AppMode::Reading && reader_is_open())
      enter_sleep(_sleep_after_turn_pending ? SLEEP_REASON_AFTER_TURN
                                           : SLEEP_REASON_TIMEOUT_READING);
    else
      enter_sleep(SLEEP_REASON_GENERIC);
  }

  delay(diagnostics_export_active() ? 1 :
        _mode == AppMode::Upload ? UPLOAD_IDLE_DELAY_MS : MAIN_LOOP_IDLE_DELAY_MS);
}