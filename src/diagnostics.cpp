#include <Arduino.h>
#include "serial_output.h"
#include <LittleFS.h>
#include <stdarg.h>
#include <esp_system.h>
#include "diagnostics.h"
#include "diagnostics_export.h"

static const char LOG_CURRENT[] = "/diagnostics.log";
static const char LOG_PREVIOUS[] = "/diagnostics.previous.log";
static const size_t LOG_MAX_BYTES = 64 * 1024;
static const unsigned long FLUSH_INTERVAL_MS = 2000;
static char _buffer[2048];
static size_t _buffered = 0;
static bool _ready = false;
static uint32_t _boot_id = 0;
static uint32_t _dropped = 0;
static unsigned long _last_flush = 0;
static char _command[24];
static size_t _command_len = 0;
static bool _command_overflow = false;

bool diagnostics_flush() {
  // The exporter owns an immutable on-disk snapshot until its files are closed.
  if (!_ready || diagnostics_export_active()) return false;
  _last_flush = millis();
  if (!_buffered) return true;

  File current;
  if (LittleFS.exists(LOG_CURRENT)) current = LittleFS.open(LOG_CURRENT, "r");
  size_t size = current ? current.size() : 0;
  current.close();
  if (size + _buffered > LOG_MAX_BYTES) {
    if (LittleFS.exists(LOG_PREVIOUS) && !LittleFS.remove(LOG_PREVIOUS)) {
      debug_serial.println("[DIAG] Cannot remove previous log; data remains buffered");
      return false;
    }
    if (!LittleFS.rename(LOG_CURRENT, LOG_PREVIOUS)) {
      debug_serial.println("[DIAG] Cannot rotate log; data remains buffered");
      return false;
    }
  }

  File file = LittleFS.open(LOG_CURRENT, "a");
  if (!file) {
    debug_serial.println("[DIAG] Cannot open log; data remains buffered");
    return false;
  }
  size_t written = file.write((const uint8_t *)_buffer, _buffered);
  file.close();
  if (written < _buffered) {
    memmove(_buffer, _buffer + written, _buffered - written);
    _buffered -= written;
    debug_serial.println("[DIAG] Incomplete log write; remaining data stays buffered");
    return false;
  }
  _buffered = 0;
  return true;
}

static bool log_message(unsigned long event_ms, const char *format, va_list args) {
  if (!_ready) {
    debug_serial.println("[DIAG] Log unavailable: filesystem not initialized");
    return false;
  }
  char message[192];
  int length = vsnprintf(message, sizeof(message), format, args);
  if (length < 0 || (size_t)length >= sizeof(message)) {
    debug_serial.println("[DIAG] Diagnostic message too long or invalid");
    return false;
  }
  char record[256];
  int count = snprintf(record, sizeof(record), "[boot=%08lx ms=%lu] %s\n",
                       (unsigned long)_boot_id, event_ms, message);
  if (count < 0 || (size_t)count >= sizeof(record)) {
    debug_serial.println("[DIAG] Cannot format diagnostic record");
    return false;
  }
  size_t bytes = (size_t)count;
  if (_buffered + bytes > sizeof(_buffer))
    diagnostics_flush();
  if (_buffered + bytes > sizeof(_buffer)) {
    _dropped++;
    debug_serial.println("[DIAG] Buffer full: diagnostic record dropped");
    return false;
  }
  memcpy(_buffer + _buffered, record, bytes);
  _buffered += bytes;
  return true;
}

bool diagnostics_log(const char *format, ...) {
  va_list args;
  va_start(args, format);
  bool recorded = log_message(millis(), format, args);
  va_end(args);
  return recorded;
}

bool diagnostics_log_at(unsigned long event_ms, const char *format, ...) {
  va_list args;
  va_start(args, format);
  bool recorded = log_message(event_ms, format, args);
  va_end(args);
  return recorded;
}

bool diagnostics_begin(uint32_t boot_id, int reset_reason, int wake_cause) {
  diagnostics_export_cancel("reinitialized");
  _ready = true;
  _boot_id = boot_id;
  _buffered = 0;
  _dropped = 0;
  _command_len = 0;
  _command_overflow = false;
  diagnostics_log("BOOT reset=%d wake=%d", reset_reason, wake_cause);
  return diagnostics_flush();
}

static void export_logs() {
  unsigned long started = millis();
  if (!serial_output_begin_export()) {
    diagnostics_log("EXPORT_FAILED already_exporting");
    return;
  }
  diagnostics_log("EXPORT_BEGIN dropped=%lu debug_dropped=%lu",
                  (unsigned long)_dropped, (unsigned long)serial_output_dropped());
  bool flushed = diagnostics_flush();
  diagnostics_export_begin(LOG_PREVIOUS, LOG_CURRENT, started,
                           flushed ? nullptr : "cannot_flush_log");
}

static void finish_export() {
  size_t bytes;
  const char *result = diagnostics_export_result(bytes);
  if (!result) return;
  if (strcmp(result, "ok") == 0) {
    diagnostics_log("EXPORT_END bytes=%u", (unsigned)bytes);
  } else {
    debug_serial.printf("[DIAG] Export aborted: %s\n", result);
    diagnostics_log("EXPORT_FAILED %s", result);
  }
  diagnostics_flush();
}

void diagnostics_poll() {
  if (!_ready) return;
  if (diagnostics_export_active()) {
    diagnostics_export_poll();
    if (!diagnostics_export_active()) finish_export();
    return;
  }
  unsigned processed = 0;
  while (Serial.available() > 0 && processed++ < 64) {
    int value = Serial.read();
    if (value < 0) break;
    char c = (char)value;
    if (c == '\r') continue;
    if (c == '\n') {
      _command[_command_len] = '\0';
      if (!_command_overflow && strcmp(_command, "LOG DUMP") == 0) {
        export_logs();
      } else {
        debug_serial.println("[DIAG] Unknown command; use LOG DUMP");
      }

      _command_len = 0;
      _command_overflow = false;
      // Remaining bytes belong to the exporter's ACK parser, not commands.
      if (diagnostics_export_active()) return;
    } else if (_command_len + 1 < sizeof(_command)) {
      _command[_command_len++] = c;
    } else {
      _command_overflow = true;
    }
  }
  if (millis() - _last_flush >= FLUSH_INTERVAL_MS)
    diagnostics_flush();
}

[[noreturn]] void diagnostics_display_timeout(uint8_t pin, unsigned long elapsed) {
  diagnostics_export_cancel("display_timeout");
  finish_export();
  diagnostics_log("ERROR display_busy_timeout gpio=%u elapsed=%lu action=restart",
                  (unsigned)pin, elapsed);
  diagnostics_flush();
  debug_serial.printf("[DISPLAY] BUSY timeout gpio=%u elapsed=%lu ms; restarting\n",
                      (unsigned)pin, elapsed);
  esp_restart();
}
