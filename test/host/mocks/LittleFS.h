#pragma once
struct LittleFSMock {
  bool begin(bool) { return true; }
  bool exists(const char *) { return true; }
};
extern LittleFSMock LittleFS;
