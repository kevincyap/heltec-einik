#include <Arduino.h>
#include <LittleFS.h>
#include <esp_sleep.h>
#include <heltec-eink-modules.h>
#include "config.h"
#include "button.h"
#include "reader.h"
#include "state.h"
#include "menu.h"

DISPLAY_TYPE display;

static void enter_sleep() {
  if (reader_is_open())
    state_save(reader_filename(), reader_current_page());

  Serial.println("Entering deep sleep...");
  Serial.flush();
  esp_sleep_enable_ext0_wakeup((gpio_num_t)USER_BUTTON_PIN, 0);
  esp_deep_sleep_start();
}

static void open_menu() {
  char selected[64];
  if (menu_show(display, selected, sizeof(selected))) {
    reader_close();
    if (reader_open(display, selected)) {
      state_save(reader_filename(), reader_current_page());
      reader_render(display);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("E-Ink Reader starting...");

  display.landscape();
  button_init();

  if (!LittleFS.begin(true)) {
    Serial.println("LittleFS mount failed!");
    return;
  }
  Serial.println("LittleFS mounted.");

  // Try to resume from saved state
  ReadingState saved = state_load();
  if (saved.valid && LittleFS.exists(saved.filename)) {
    Serial.printf("Resuming: %s page %d\n", saved.filename, saved.page);
    if (reader_open(display, saved.filename, saved.page)) {
      reader_render(display);
      return;
    }
  }

  open_menu();
}

void loop() {
  ButtonEvent evt = button_poll();

  if (reader_is_open()) {
    switch (evt) {
      case BTN_SHORT:
        if (reader_next_page()) {
          reader_render(display);
          state_save(reader_filename(), reader_current_page());
        }
        break;
      case BTN_LONG:
        if (reader_prev_page()) {
          reader_render(display);
          state_save(reader_filename(), reader_current_page());
        }
        break;
      case BTN_MENU:
        open_menu();
        break;
      default:
        break;
    }
  }

  // Deep sleep on inactivity
  if (millis() - button_last_activity() > SLEEP_TIMEOUT_MS)
    enter_sleep();

  delay(10);
}