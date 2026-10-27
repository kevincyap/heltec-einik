#include <Arduino.h>
#include "serial_output.h"
#include <LittleFS.h>
#include "config.h"
#include "state.h"

static char _cached_filename[64] = {};
static int _cached_page = -1;
static bool _cache_valid = false;
static ReadingState _cached_book = {};

#pragma pack(push, 1)
struct StateData {
  char filename[64];
  int page;
};
#pragma pack(pop)

ReadingState state_load() {
  ReadingState result = {};
  state_load_checked(result);
  return result;
}

bool state_load_checked(ReadingState &result) {
  result = {};
  _cache_valid = false;
  if (!LittleFS.exists(STATE_FILE)) return true;
  File f = LittleFS.open(STATE_FILE, "r");
  if (!f) {
    debug_serial.printf("State read failed: %s\n", STATE_FILE);
    return false;
  }

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
  if (!result.valid)
    debug_serial.printf("State read incomplete: %s\n", STATE_FILE);
  return result.valid;
}

static bool write_atomic(const char *path, const uint8_t *data, size_t size) {
  char temp[80];
  snprintf(temp, sizeof(temp), "%s.tmp", path);
  if (LittleFS.exists(temp) && !LittleFS.remove(temp)) return false;
  File f = LittleFS.open(temp, "w");
  if (!f) return false;
  bool written = f.write(data, size) == size;
  f.close();
  if (written && LittleFS.rename(temp, path)) return true;
  LittleFS.remove(temp);
  return false;
}

bool state_save(const char *filename, int page) {
  if (_cache_valid && _cached_page == page && strncmp(_cached_filename, filename, sizeof(_cached_filename)) == 0) {
    return true;
  }

  StateData data = {};
  strncpy(data.filename, filename, sizeof(data.filename) - 1);
  data.page = page;

  unsigned long started = millis();
  bool written = write_atomic(STATE_FILE, (const uint8_t *)&data, sizeof(data));
  if (written) {
    strncpy(_cached_filename, data.filename, sizeof(_cached_filename) - 1);
    _cached_filename[sizeof(_cached_filename) - 1] = '\0';
    _cached_page = data.page;
    _cache_valid = true;
  } else {
    _cache_valid = false;
    debug_serial.printf("State write incomplete: %s\n", STATE_FILE);
  }
  unsigned long elapsed = millis() - started;
  if (elapsed >= 50)
    debug_serial.printf("[STATE] page=%d save=%lu ms\n", page + 1, elapsed);
  return written;
}

bool state_clear() {
  if (LittleFS.exists(STATE_FILE) && !LittleFS.remove(STATE_FILE)) return false;
  _cache_valid = false;
  _cached_filename[0] = '\0';
  _cached_page = -1;
  return true;
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
  if (!f) {
    _cached_book.valid = false;
    return 0;
  }
  int page = 0;
  bool valid = f.read((uint8_t *)&page, sizeof(page)) == sizeof(page);
  f.close();
  if (!valid) {
    debug_serial.printf("Bookmark read incomplete: %s\n", path);
    _cached_book.valid = false;
    return 0;
  }
  strncpy(_cached_book.filename, filename, sizeof(_cached_book.filename) - 1);
  _cached_book.filename[sizeof(_cached_book.filename) - 1] = '\0';
  _cached_book.page = page;
  _cached_book.valid = true;
  return page;
}

bool state_save_book_page(const char *filename, int page) {
  if (_cached_book.valid && _cached_book.page == page &&
      strncmp(_cached_book.filename, filename, sizeof(_cached_book.filename)) == 0)
    return true;

  char path[72];
  pos_path_for(filename, path, sizeof(path));
  bool written = write_atomic(path, (const uint8_t *)&page, sizeof(page));
  if (!written) {
    _cached_book.valid = false;
    debug_serial.printf("Bookmark write incomplete: %s\n", path);
    return false;
  }
  strncpy(_cached_book.filename, filename, sizeof(_cached_book.filename) - 1);
  _cached_book.filename[sizeof(_cached_book.filename) - 1] = '\0';
  _cached_book.page = page;
  _cached_book.valid = true;
  return true;
}

bool state_delete_book_page(const char *filename) {
  char path[72];
  pos_path_for(filename, path, sizeof(path));
  if (LittleFS.exists(path) && !LittleFS.remove(path)) return false;
  if (strncmp(_cached_book.filename, filename, sizeof(_cached_book.filename)) == 0)
    _cached_book.valid = false;
  return true;
}
