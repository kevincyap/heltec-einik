#pragma once
#include <Arduino.h>
#include <stdarg.h>
#include "diagnostics.h"

struct DiagnosticRecord {
  unsigned long when;
  std::string text;
};
static std::vector<DiagnosticRecord> diagnostic_records;
static int diagnostic_flushes = 0;
static bool record_diagnostic(unsigned long when, const char *format, va_list args) {
  char text[256];
  vsnprintf(text, sizeof(text), format, args);
  diagnostic_records.push_back({when, text});
  return true;
}
inline bool diagnostics_log(const char *format, ...) {
  va_list args;
  va_start(args, format);
  bool result = record_diagnostic(millis(), format, args);
  va_end(args);
  return result;
}
inline bool diagnostics_log_at(unsigned long when, const char *format, ...) {
  va_list args;
  va_start(args, format);
  bool result = record_diagnostic(when, format, args);
  va_end(args);
  return result;
}
inline bool diagnostics_begin(uint32_t, int, int) { return true; }
static bool diagnostic_exporting = false;
inline bool diagnostics_export_active() { return diagnostic_exporting; }
inline bool diagnostics_flush() { diagnostic_flushes++; return !diagnostic_exporting; }
static unsigned long diagnostic_poll_delay = 0;
inline void diagnostics_poll() { delay(diagnostic_poll_delay); }
struct DisplayTimeout {
  uint8_t pin;
  unsigned long elapsed;
};
[[noreturn]] inline void diagnostics_display_timeout(uint8_t pin, unsigned long elapsed) {
  diagnostics_log("ERROR display_busy_timeout gpio=%u elapsed=%lu action=restart",
                  (unsigned)pin, elapsed);
  diagnostics_flush();
  throw DisplayTimeout{pin, elapsed};
}
