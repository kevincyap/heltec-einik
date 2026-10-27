#pragma once
#include <stddef.h>
#include <stdint.h>

class DebugSerialOutput {
 public:
  void printf(const char *format, ...);
  void println(const char *text);
};

extern DebugSerialOutput debug_serial;
void serial_output_begin();
bool serial_output_begin_export();
void serial_output_end_export();
size_t serial_output_try_write(const uint8_t *data, size_t count);
uint32_t serial_output_dropped();
