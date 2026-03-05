#include <Arduino.h>
#include <LittleFS.h>
#include <esp_sleep.h>
#include "config.h"
#include "menu.h"
#include "button.h"
#include <Fonts/FreeSans9pt7b.h>

#define MAX_FILES 20

static const char UPLOAD_ENTRY[] = "[Upload Books]";

static void render_menu(DISPLAY_TYPE &display, char files[][64], int count,
                        bool has_upload, int sel) {
  display.clearMemory();
  display.fillRect(0, 0, display.width(), display.height(), WHITE);

  // Title bar (small default font)
  display.setFont(NULL);
  display.setTextSize(1);
  display.setTextColor(BLACK);
  display.setCursor(2, 2);
  display.print("Select a book:");

  // File list
  display.setFont(&FreeSans9pt7b);
  int list_top = 20;
  int max_visible = (display.height() - list_top - MARGIN_BOTTOM) / LINE_HEIGHT;

  int total_items = count + (has_upload ? 1 : 0);
  int scroll = 0;
  if (sel >= max_visible)
    scroll = sel - max_visible + 1;

  int y = list_top + MARGIN_TOP - 4;
  for (int i = scroll; i < total_items && (i - scroll) < max_visible; i++) {
    if (i == sel) {
      display.fillRect(0, y - LINE_HEIGHT + 4, display.width(), LINE_HEIGHT, BLACK);
      display.setTextColor(WHITE);
    } else {
      display.setTextColor(BLACK);
    }

    display.setCursor(MARGIN_X + 2, y);
    if (has_upload && i == total_items - 1) {
      // Last item is the upload option
      display.print(UPLOAD_ENTRY);
    } else {
      // Display filename without leading slash
      const char *name = files[i];
      if (name[0] == '/') name++;
      display.print(name);
    }
    y += LINE_HEIGHT;
  }

  display.setTextColor(BLACK);

  display.fastmodeOn();
  display.update();
}

bool menu_show(DISPLAY_TYPE &display, char *selected, size_t max_len) {
  char files[MAX_FILES][64];
  int file_count = 0;

  File root = LittleFS.open("/");
  if (!root || !root.isDirectory()) return false;

  File f = root.openNextFile();
  while (f && file_count < MAX_FILES) {
    if (!f.isDirectory()) {
      String name = f.name();
      if (name.endsWith(".txt")) {
        // Normalize path to always have leading /
        if (name[0] != '/')
          snprintf(files[file_count], 64, "/%s", name.c_str());
        else
          strncpy(files[file_count], name.c_str(), 64);
        files[file_count][63] = '\0';
        file_count++;
      }
    }
    f = root.openNextFile();
  }
  root.close();

  int sel = 0;
  bool has_upload = true;
  int total_items = file_count + (has_upload ? 1 : 0);
  render_menu(display, files, file_count, has_upload, sel);

  while (true) {
    ButtonEvent evt = button_poll();
    switch (evt) {
      case BTN_SHORT:
        sel = (sel + 1) % total_items;
        render_menu(display, files, file_count, has_upload, sel);
        break;
      case BTN_LONG:
      case BTN_MENU:
        if (has_upload && sel == total_items - 1) {
          // Upload mode selected — signal with empty string
          selected[0] = '\0';
          return true;
        }
        strncpy(selected, files[sel], max_len - 1);
        selected[max_len - 1] = '\0';
        return true;
      default:
        // Sleep if idle too long on menu screen
        if (millis() - button_last_activity() > SLEEP_TIMEOUT_MS) {
          esp_sleep_enable_ext0_wakeup((gpio_num_t)USER_BUTTON_PIN, 0);
          esp_deep_sleep_start();
        }
        delay(10);
        break;
    }
  }
}
