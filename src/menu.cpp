#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"
#include "menu.h"
#include "button.h"
#include "book_files.h"
#include <Fonts/FreeSans9pt7b.h>

#define MAX_FILES 20

static const char UPLOAD_ENTRY[] = "[Upload Books]";
static char files[MAX_FILES][64];
static int file_count;
static int sel;

static void render_menu(EinkDisplay &display, char files[][64], int count,
                        int sel) {
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

  int total_items = count + 1;
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
    if (i == count) {
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

bool menu_begin(EinkDisplay &display) {
  file_count = 0;
  sel = 0;

  File root = LittleFS.open("/");
  if (!root || !root.isDirectory()) return false;

  File f = root.openNextFile();
  while (f && file_count < MAX_FILES) {
    if (!f.isDirectory()) {
      const char *name = f.name();
      if (has_txt_extension(name)) {
        // Normalize path to always have leading /
        if (name[0] != '/')
          snprintf(files[file_count], 64, "/%s", name);
        else
          strncpy(files[file_count], name, 64);
        files[file_count][63] = '\0';
        file_count++;
      }
    }
    f = root.openNextFile();
  }
  root.close();

  render_menu(display, files, file_count, sel);
  return true;
}

MenuResult menu_handle_event(EinkDisplay &display, ButtonEvent event) {
  int total_items = file_count + 1;
  switch (event) {
    case BTN_SHORT:
    case BTN_DOWN:
      sel = (sel + 1) % total_items;
      render_menu(display, files, file_count, sel);
      break;
    case BTN_UP:
      sel = (sel + total_items - 1) % total_items;
      render_menu(display, files, file_count, sel);
      break;
    case BTN_LONG:
    case BTN_SELECT:
#if !INPUT_SINGLE_BUTTON
    case BTN_MENU:
#endif
      if (sel == file_count)
        return {MenuAction::Upload, nullptr};
      return {MenuAction::Book, files[sel]};
    default:
      break;
  }
  return {MenuAction::None, nullptr};
}
