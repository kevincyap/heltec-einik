#include <Arduino.h>
#include <LittleFS.h>
#include <esp_sleep.h>
#include <heltec-eink-modules.h>
#include "config.h"
#include "button.h"
#include "reader.h"
#include "state.h"
#include "menu.h"
#include "wifi_upload.h"

DISPLAY_TYPE display;
static bool _sleep_after_turn_pending = false;
static unsigned long _sleep_after_turn_started_ms = 0;
RTC_DATA_ATTR static bool _wake_turn_page_pending = false;

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
  Serial.printf("Sleep reason=%s, set wake_flag=%d\n",
                sleep_reason_name(reason),
                _wake_turn_page_pending ? 1 : 0);

  if (reader_is_open())
    state_save(reader_filename(), reader_current_page());

  Serial.println("Entering deep sleep...");
  Serial.flush();
  esp_sleep_enable_ext0_wakeup((gpio_num_t)USER_BUTTON_PIN, 0);
  esp_deep_sleep_start();
}

static unsigned long wait_for_wake_button_release() {
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT0)
    return 0;

  unsigned long start = millis();
  while (digitalRead(USER_BUTTON_PIN) == LOW && (millis() - start) < WAKE_BUTTON_RELEASE_TIMEOUT_MS) {
    delay(10);
  }

  return millis() - start;
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
    if (evt == BTN_SHORT || evt == BTN_LONG || evt == BTN_MENU) {
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

    reader_close();
    if (reader_open(display, selected)) {
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
  Serial.println("E-Ink Reader starting...");

  esp_sleep_wakeup_cause_t wake_cause = esp_sleep_get_wakeup_cause();
  bool woke_from_button = (wake_cause == ESP_SLEEP_WAKEUP_EXT0);
  Serial.printf("Wake cause=%d, rtc wake_flag(before)=%d\n",
                (int)wake_cause,
                _wake_turn_page_pending ? 1 : 0);

  bool should_turn_page_after_wake = (woke_from_button && _wake_turn_page_pending);
  _wake_turn_page_pending = false;
  Serial.printf("Wake decision: woke_from_button=%d, auto_advance=%d, rtc wake_flag(after)=%d\n",
                woke_from_button ? 1 : 0,
                should_turn_page_after_wake ? 1 : 0,
                _wake_turn_page_pending ? 1 : 0);

  display.setRotation(USB_LEFT);
  pinMode(USER_BUTTON_PIN, INPUT_PULLUP);
  unsigned long wake_hold_ms = wait_for_wake_button_release();
  bool wake_prev_requested = (should_turn_page_after_wake && wake_hold_ms >= LONG_PRESS_MS);
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

  // Try to resume from saved state
  ReadingState saved = state_load();
  if (saved.valid && LittleFS.exists(saved.filename)) {
    Serial.printf("Resuming: %s page %d\n", saved.filename, saved.page);
    if (reader_open(display, saved.filename, saved.page)) {
      reader_render(display);

      if (should_turn_page_after_wake) {
        bool turned = wake_prev_requested ? reader_prev_page() : reader_next_page();
        if (turned) {
          Serial.printf("Wake turn applied: %s -> page %d\n",
                        wake_prev_requested ? "prev" : "next",
                        reader_current_page());
          reader_render(display);
        } else {
          Serial.println("Wake turn requested at boundary; staying on current page");
        }
      }

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
        if (reader_next_page()) {
          reader_render(display);
          state_save(reader_filename(), reader_current_page());
#if SLEEP_AFTER_PAGE_TURN
          _sleep_after_turn_pending = true;
          _sleep_after_turn_started_ms = millis();
#endif
        }
        break;
      case BTN_LONG:
        if (reader_prev_page()) {
          reader_render(display);
          state_save(reader_filename(), reader_current_page());
#if SLEEP_AFTER_PAGE_TURN
          _sleep_after_turn_pending = true;
          _sleep_after_turn_started_ms = millis();
#endif
        }
        break;
      case BTN_MENU:
        _sleep_after_turn_pending = false;
        open_menu();
        break;
      default:
        break;
    }

#if SLEEP_AFTER_PAGE_TURN
    if (_sleep_after_turn_pending && (millis() - _sleep_after_turn_started_ms >= SLEEP_AFTER_PAGE_TURN_DELAY_MS)) {
      enter_sleep(SLEEP_REASON_AFTER_TURN);
    }
#endif
  }

  // Deep sleep on inactivity
  if (millis() - button_last_activity() > SLEEP_TIMEOUT_MS) {
    if (reader_is_open())
      enter_sleep(SLEEP_REASON_TIMEOUT_READING);
    else
      enter_sleep(SLEEP_REASON_GENERIC);
  }

  delay(MAIN_LOOP_IDLE_DELAY_MS);
}