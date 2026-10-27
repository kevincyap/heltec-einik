#include <Arduino.h>
#include <atomic>
#include <stdarg.h>
#include "serial_output.h"

DebugSerialOutput debug_serial;
static std::atomic_flag _writer = ATOMIC_FLAG_INIT;
static std::atomic<bool> _exporting(false);
static std::atomic<uint32_t> _dropped_debug(0);

void serial_output_begin() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  // HWCDC's zero-timeout retry counter can underflow.
  Serial.setTxTimeoutMs(1);
#endif
}

bool serial_output_begin_export() {
  return !_exporting.exchange(true);
}

void serial_output_end_export() { _exporting.store(false); }
uint32_t serial_output_dropped() { return _dropped_debug.load(); }

size_t serial_output_try_write(const uint8_t *data, size_t count) {
  if (_writer.test_and_set()) return 0;
  size_t written = 0;
  if (Serial) {
    int available = Serial.availableForWrite();
    if (available > 0) {
      if (count > 64) count = 64;
      if (count > (size_t)available) count = (size_t)available;
      written = Serial.write(data, count);
    }
  }
  _writer.clear();
  return written;
}

static void debug_write(const char *text, size_t length) {
  if (_exporting.load() || _writer.test_and_set()) {
    _dropped_debug++;
    return;
  }
  if (!_exporting.load() && Serial &&
      Serial.availableForWrite() >= (int)length) {
    if (Serial.write((const uint8_t *)text, length) != length)
      _dropped_debug++;
  } else {
    _dropped_debug++;
  }
  _writer.clear();
}

void DebugSerialOutput::printf(const char *format, ...) {
  char text[256];
  va_list args;
  va_start(args, format);
  int length = vsnprintf(text, sizeof(text), format, args);
  va_end(args);
  if (length < 0 || (size_t)length >= sizeof(text)) {
    _dropped_debug++;
    return;
  }
  debug_write(text, (size_t)length);
}

void DebugSerialOutput::println(const char *text) {
  printf("%s\n", text);
}
