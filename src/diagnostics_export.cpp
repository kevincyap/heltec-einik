#include <Arduino.h>
#include <LittleFS.h>
#include "diagnostics_export.h"
#include "serial_output.h"

namespace {
const unsigned long EXPORT_TIMEOUT_MS = 15000;
const unsigned long PROGRESS_TIMEOUT_MS = 2000;
enum class Phase { Idle, Header, Ready, Load, Payload, Ack, Trailer, Error };
Phase phase = Phase::Idle;
File previous_file, current_file;
size_t previous_left, current_left, total_bytes;
uint8_t chunk[256];
char control[80], reply[16];
size_t send_size, sent, reply_used;
uint32_t crc;
unsigned long started, progressed;
const char *result = nullptr;

uint32_t update_crc(uint32_t value, const uint8_t *data, size_t count) {
  while (count--) {
    value ^= *data++;
    for (int bit = 0; bit < 8; bit++)
      value = (value >> 1) ^ ((value & 1) ? 0xedb88320UL : 0);
  }
  return value;
}

void finish(const char *reason) {
  previous_file.close();
  current_file.close();
  phase = Phase::Idle;
  result = reason;
  serial_output_end_export();
}

void sending(Phase next, size_t size) {
  phase = next;
  send_size = size;
  sent = 0;
  progressed = millis();
}

void waiting(Phase next) {
  phase = next;
  reply_used = 0;
  progressed = millis();
}
}

bool diagnostics_export_active() { return phase != Phase::Idle; }

void diagnostics_export_cancel(const char *reason) {
  if (diagnostics_export_active()) finish(reason);
}

const char *diagnostics_export_result(size_t &bytes) {
  bytes = total_bytes;
  const char *value = result;
  result = nullptr;
  return value;
}

void diagnostics_export_begin(const char *previous, const char *current,
                              unsigned long started_at, const char *error) {
  result = nullptr;
  total_bytes = 0;
  started = started_at;
  progressed = millis();
  if (!error) {
    bool has_previous = LittleFS.exists(previous);
    if (has_previous) previous_file = LittleFS.open(previous, "r");
    current_file = LittleFS.open(current, "r");
    if (!current_file || (has_previous && !previous_file))
      error = "cannot_open_log";
  }
  if (error) {
    result = error;
    sending(Phase::Error, (size_t)snprintf(control, sizeof(control),
                                         "@@DIAG_ERROR %s\n", error));
    return;
  }
  previous_left = previous_file ? previous_file.size() : 0;
  current_left = current_file.size();
  total_bytes = previous_left + current_left;
  crc = 0xffffffffUL;
  sending(Phase::Header, (size_t)snprintf(control, sizeof(control),
                      "@@DIAG_BEGIN2 %u 256\n", (unsigned)total_bytes));
}

void diagnostics_export_poll() {
  // Each poll has at most four bounded serial writes, one chunk read (across
  // at most two files), and 64 RX bytes. No ACK or USB progress wait loops.
  unsigned writes = 0, received = 0;
  bool loaded = false;
  for (unsigned step = 0; step < 8 && diagnostics_export_active(); step++) {
    if (!Serial) { finish("usb_disconnected"); break; }
    if (millis() - started >= EXPORT_TIMEOUT_MS) {
      finish("overall_timeout"); break;
    }
    if (millis() - progressed >= PROGRESS_TIMEOUT_MS) {
      finish("progress_timeout"); break;
    }
    if (phase == Phase::Ready || phase == Phase::Ack) {
      const char *expected = phase == Phase::Ready ? "LOG READY" : "LOG ACK";
      bool complete = false;
      while (received < 64 && Serial.available() > 0) {
        int value = Serial.read();
        received++;
        if (value < 0) break;
        if (value == '\r') continue;
        if (value == '\n') {
          reply[reply_used] = '\0';
          if (strcmp(reply, expected) != 0) {
            finish("invalid_ack"); return;
          }
          phase = Phase::Load;
          progressed = millis();
          complete = true;
          break;
        }
        if (reply_used + 1 >= sizeof(reply)) {
          finish("invalid_ack"); return;
        }
        reply[reply_used++] = (char)value;
      }
      if (!complete) break;
    } else if (phase == Phase::Load) {
      if (!previous_left && !current_left) {
        sending(Phase::Trailer, (size_t)snprintf(control, sizeof(control),
               "\n@@DIAG_END %08lx\n", (unsigned long)(crc ^ 0xffffffffUL)));
      } else {
        if (loaded) break;
        loaded = true;
        size_t used = 0;
        size_t count = previous_left < sizeof(chunk) ? previous_left : sizeof(chunk);
        if (count && previous_file.read(chunk, count) != count) {
          finish("read_error"); break;
        }
        previous_left -= count;
        used += count;
        count = current_left < sizeof(chunk) - used ? current_left : sizeof(chunk) - used;
        if (count && current_file.read(chunk + used, count) != count) {
          finish("read_error"); break;
        }
        current_left -= count;
        used += count;
        crc = update_crc(crc, chunk, used);
        sending(Phase::Payload, used);
      }
    } else {
      if (writes >= 4) break;
      writes++;
      const uint8_t *data = phase == Phase::Payload ? chunk : (const uint8_t *)control;
      size_t count = serial_output_try_write(data + sent, send_size - sent);
      if (!count) break;
      sent += count;
      progressed = millis();
      if (sent == send_size) {
        if (phase == Phase::Header) waiting(Phase::Ready);
        else if (phase == Phase::Payload) waiting(Phase::Ack);
        else if (phase == Phase::Trailer) finish("ok");
        else finish(result);
      }
    }
  }
}
