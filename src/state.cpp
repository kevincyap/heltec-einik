#include <Arduino.h>
#include <LittleFS.h>
#include "config.h"
#include "state.h"

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
  }
  f.close();
  return result;
}

void state_save(const char *filename, int page) {
  StateData data = {};
  strncpy(data.filename, filename, sizeof(data.filename) - 1);
  data.page = page;

  File f = LittleFS.open(STATE_FILE, "w");
  if (f) {
    f.write((uint8_t *)&data, sizeof(data));
    f.close();
  }
}

void state_clear() {
  LittleFS.remove(STATE_FILE);
}
