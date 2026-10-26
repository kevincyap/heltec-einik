#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"
#include "state.h"

static char _cached_filename[64] = {};
static int _cached_page = -1;
static bool _cache_valid = false;

#pragma pack(push, 1)
struct StateData {
  char filename[64];
  int page;
};
#pragma pack(pop)

ReadingState state_load() {
  ReadingState result = {};
  result.valid = false;

  File f = LittleFS.open(STATE_FILE, "r");
  if (!f) return result;

  StateData data;
  if (f.read((uint8_t *)&data, sizeof(data)) == sizeof(data)) {
    strncpy(result.filename, data.filename, sizeof(result.filename) - 1);
    result.page = data.page;
    result.valid = true;
    strncpy(_cached_filename, result.filename, sizeof(_cached_filename) - 1);
    _cached_filename[sizeof(_cached_filename) - 1] = '\0';
    _cached_page = result.page;
    _cache_valid = true;
  }
  f.close();
  return result;
}

void state_save(const char *filename, int page) {
  if (_cache_valid && _cached_page == page && strncmp(_cached_filename, filename, sizeof(_cached_filename)) == 0) {
    return;
  }

  StateData data = {};
  strncpy(data.filename, filename, sizeof(data.filename) - 1);
  data.page = page;

  unsigned long started = millis();
  File f = LittleFS.open(STATE_FILE, "w");
  if (f) {
    f.write((uint8_t *)&data, sizeof(data));
    f.close();
    strncpy(_cached_filename, data.filename, sizeof(_cached_filename) - 1);
    _cached_filename[sizeof(_cached_filename) - 1] = '\0';
    _cached_page = data.page;
    _cache_valid = true;
  }
  unsigned long elapsed = millis() - started;
  if (elapsed >= 50)
    Serial.printf("[STATE] page=%d save=%lu ms\n", page + 1, elapsed);
}

void state_clear() {
  LittleFS.remove(STATE_FILE);
  _cache_valid = false;
  _cached_filename[0] = '\0';
  _cached_page = -1;
}

// -- Per-book page position (.pos sidecar files) --

static void pos_path_for(const char *filename, char *out, size_t out_len) {
  // "/mobydick.txt" -> "/mobydick.pos"
  const char *dot = strrchr(filename, '.');
  size_t base_len = dot ? (size_t)(dot - filename) : strlen(filename);
  if (base_len + 5 >= out_len) base_len = out_len - 5; // ".pos" + NUL
  memcpy(out, filename, base_len);
  memcpy(out + base_len, ".pos", 5); // includes NUL
}

int state_load_book_page(const char *filename) {
  char path[72];
  pos_path_for(filename, path, sizeof(path));
  File f = LittleFS.open(path, "r");
  if (!f) return 0;
  int page = 0;
  if (f.read((uint8_t *)&page, sizeof(page)) != sizeof(page)) page = 0;
  f.close();
  return page;
}

void state_save_book_page(const char *filename, int page) {
  char path[72];
  pos_path_for(filename, path, sizeof(path));
  File f = LittleFS.open(path, "w");
  if (f) {
    f.write((uint8_t *)&page, sizeof(page));
    f.close();
  }
}

void state_delete_book_page(const char *filename) {
  char path[72];
  pos_path_for(filename, path, sizeof(path));
  LittleFS.remove(path);
}
