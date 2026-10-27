#include <cassert>
#include <Arduino.h>
#include "serial_output.h"

unsigned long now_ms;
int pin_modes[49];
int pin_levels[49];
SerialMock Serial;

int main() {
  serial_output_begin();
  assert(Serial.tx_timeout_ms == 1);
  debug_serial.printf("page=%d\n", 3);
  assert(Serial.output == "page=3\n");
  size_t calls = Serial.write_calls;
  Serial.backpressured = true;
  for (int i = 0; i < 1000; i++) debug_serial.println("must_not_block");
  assert(Serial.write_calls == calls && now_ms == 0);
  assert(serial_output_dropped() == 1000);
  const uint8_t bytes[128] = {};
  assert(serial_output_try_write(bytes, sizeof(bytes)) == 0);
  assert(Serial.write_calls == calls);
  Serial.backpressured = false;
  Serial.connected = false;
  debug_serial.println("disconnected");
  assert(serial_output_try_write(bytes, sizeof(bytes)) == 0);
  assert(Serial.write_calls == calls);
  Serial.connected = true;
  assert(serial_output_begin_export());
  assert(!serial_output_begin_export());
  debug_serial.println("must_not_interleave");
  assert(Serial.write_calls == calls);
  assert(serial_output_try_write(bytes, sizeof(bytes)) == 64);
  serial_output_end_export();
  Serial.write_limit = 3;
  assert(serial_output_try_write(bytes, sizeof(bytes)) == 3);
  calls = Serial.write_calls;
  Serial.write_limit = 0;
  assert(serial_output_try_write(bytes, sizeof(bytes)) == 0);
  assert(Serial.write_calls == calls);
  puts("Guarded serial connection, backpressure, export isolation and chunk limits passed");
}
