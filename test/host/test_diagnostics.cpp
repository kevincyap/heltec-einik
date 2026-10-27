#include <cassert>
#include <climits>
#include "../../src/diagnostics_export.cpp"
#include "../../src/diagnostics.cpp"

unsigned long now_ms;
int pin_modes[49];
int pin_levels[49];
SerialMock Serial;
LittleFSMock LittleFS;

static std::string stored(const char *path = LOG_CURRENT) {
  const auto &bytes = LittleFS.storage[path];
  return std::string(bytes.begin(), bytes.end());
}

static void reset() {
  diagnostics_export_cancel("test_reset");
  Serial = SerialMock();
  LittleFS = LittleFSMock();
  now_ms = 0;
  assert(diagnostics_begin(0x12345678, 1, 0));
  Serial.output.clear();
}

static void poll(unsigned long advance = 1) {
  size_t calls = Serial.write_calls, output = Serial.output.size();
  unsigned long before = now_ms;
  size_t unread = previous_left + current_left;
  bool exporting = diagnostics_export_active();
  diagnostics_poll();
  assert(now_ms == before);
  // Four export writes plus at most one terminal error debug message.
  assert(Serial.write_calls - calls <= 5);
  assert(Serial.output.size() - output <= 512);
  if (exporting) {
    assert(unread - (previous_left + current_left) <= 256);
    if (diagnostics_export_active()) {
      assert(Serial.write_calls - calls <= 4);
      assert(Serial.output.size() - output <= 256);
    }
  }
  now_ms += advance;
}

static void start() {
  Serial.input = "LOG DUMP\r\n";
  poll();
  assert(diagnostics_export_active());
}

static void until_done(unsigned limit = 20000, unsigned long advance = 1) {
  for (unsigned i = 0; diagnostics_export_active() && i < limit; i++)
    poll(advance);
  assert(!diagnostics_export_active());
  assert(serial_output_begin_export());
  serial_output_end_export();
}

static bool ready_sent;
static size_t acknowledged;
static unsigned advertised;
static void downloader() {
  ready_sent = false;
  acknowledged = advertised = 0;
  Serial.on_write = []() {
    size_t end = Serial.output.find('\n');
    if (end == std::string::npos) return;
    if (!ready_sent) {
      assert(sscanf(Serial.output.c_str(), "@@DIAG_BEGIN2 %u 256", &advertised) == 1);
      Serial.input += "LOG READY\r\n";
      ready_sent = true;
    }
    size_t target = std::min(acknowledged + 256, (size_t)advertised);
    if (acknowledged < advertised && Serial.output.size() - end - 1 >= target) {
      Serial.input += "LOG ACK\n";
      acknowledged = target;
    }
  };
}

static uint32_t reference_crc(const std::string &text) {
  uint32_t value = ~uint32_t(0);
  for (unsigned char c : text) {
    value ^= c;
    for (int bit = 0; bit < 8; bit++)
      value = value & 1 ? (value >> 1) ^ 0xedb88320UL : value >> 1;
  }
  return ~value;
}

static void test_recording() {
  reset();
  assert(stored().find("boot=12345678") != std::string::npos);
  size_t initial_size = stored().size();
  now_ms = 100;
  assert(diagnostics_log_at(50, "BUG_MARKER page=%d", 3));
  assert(stored().size() == initial_size);
  now_ms = FLUSH_INTERVAL_MS;
  poll();
  assert(stored().find("ms=50] BUG_MARKER page=3") != std::string::npos);
  assert(_buffered == 0);

  diagnostics_log("partial_write");
  std::string expected = stored() + std::string(_buffer, _buffered);
  LittleFS.write_limit = 5;
  assert(!diagnostics_flush() && _buffered > 0);
  LittleFS.write_limit = size_t(-1);
  assert(diagnostics_flush() && stored() == expected);

  for (char c : {'R', 'S'}) {
    LittleFS.storage[LOG_CURRENT].resize(LOG_MAX_BYTES - 1, c);
    LittleFS.storage[LOG_CURRENT].back() = c;
    diagnostics_log("rotation");
    assert(diagnostics_flush());
    assert(LittleFS.storage[LOG_PREVIOUS].size() == LOG_MAX_BYTES - 1);
    assert(LittleFS.storage[LOG_PREVIOUS].back() == c);
  }
  std::string before = stored();
  assert(diagnostics_begin(0x87654321, 5, 2));
  assert(stored().find(before) == 0);
  assert(stored().find("boot=87654321") != std::string::npos);

  reset();
  LittleFS.storage[LOG_CURRENT].resize(LOG_MAX_BYTES - 1, 'R');
  LittleFS.files.push_back(LOG_PREVIOUS); // exists, but cannot be removed.
  diagnostics_log("rotation_failure");
  assert(!diagnostics_flush() && _buffered > 0);
  LittleFS.files.clear();
  assert(diagnostics_flush());
  assert(stored().find("rotation_failure") != std::string::npos);
}

static void test_transfer(bool large, bool rollover) {
  reset();
  if (rollover) now_ms = ULONG_MAX - 100;
  if (large) {
    LittleFS.storage[LOG_PREVIOUS].assign(LOG_MAX_BYTES - 3, 'P');
    LittleFS.storage[LOG_CURRENT].assign(LOG_MAX_BYTES - 200, 'C');
  }
  Serial.write_limit = large ? 64 : 7;
  downloader();
  start();
  std::string previous = stored(LOG_PREVIOUS), current = stored();
  std::string snapshot = previous + current;
  int opens = LittleFS.write_opens;
  unsigned long began = now_ms;
  unsigned navigation_events = 0;
  while (diagnostics_export_active()) {
    navigation_events++;
    diagnostics_log("NAV next page=%u", navigation_events);
    assert(!diagnostics_flush());
    size_t before = Serial.output.size();
    debug_serial.println("must_not_corrupt_payload");
    assert(Serial.output.size() == before);
    poll();
    if (diagnostics_export_active()) {
      assert(stored(LOG_PREVIOUS) == previous && stored() == current);
      assert(LittleFS.write_opens == opens);
    }
    assert(now_ms - began < 15000);
  }
  Serial.on_write = {};
  assert(acknowledged == advertised && advertised == snapshot.size());
  if (large) assert(advertised > 130000 && _dropped > 0);
  size_t header_size = Serial.output.find('\n') + 1;
  assert(Serial.output.substr(header_size, advertised) == snapshot);
  unsigned actual_crc = 0;
  assert(sscanf(Serial.output.c_str() + header_size + advertised,
                "\n@@DIAG_END %x", &actual_crc) == 1);
  assert(actual_crc == reference_crc(snapshot));
  assert(Serial.input.empty());
  assert(_buffered == 0 && LittleFS.write_opens > opens);
  std::string logs = stored(LOG_PREVIOUS) + stored();
  assert(logs.find("NAV next") != std::string::npos);
  assert(logs.find("EXPORT_END bytes=") != std::string::npos);
  assert(serial_output_begin_export());
  serial_output_end_export();
}

static void failed_with(const char *reason) {
  assert(!diagnostics_export_active());
  assert((stored(LOG_PREVIOUS) + stored()).find(
           std::string("EXPORT_FAILED ") + reason) != std::string::npos);
  assert(serial_output_begin_export());
  serial_output_end_export();
}

static void test_failures() {
  for (const char *ack : {"WRONG ACK\n", "LOG ACK\n", "12345678901234567\n"}) {
    reset();
    start();
    Serial.input = ack;
    until_done();
    failed_with("invalid_ack");
  }
  for (bool ready : {false, true}) {
    reset();
    start();
    Serial.input = ready ? "LOG READY\n" : "LOG REA";
    until_done(3000);
    failed_with("progress_timeout");
  }
  for (bool disconnected : {false, true}) {
    reset();
    start();
    Serial.backpressured = !disconnected;
    Serial.connected = !disconnected;
    until_done(3000);
    failed_with(disconnected ? "usb_disconnected" : "progress_timeout");
  }
  reset();
  now_ms = ULONG_MAX - 100;
  start();
  until_done(3000);
  failed_with("progress_timeout");

  reset();
  LittleFS.storage[LOG_CURRENT].assign(60000, 'C');
  downloader();
  start();
  until_done(100, 1000);
  Serial.on_write = {};
  failed_with("overall_timeout");

  reset();
  start();
  Serial.write_limit = 1;
  Serial.input = "LOG READY\n";
  until_done(100, 1000); // TX progresses without an ACK wait; global limit still applies.
  failed_with("overall_timeout");

  reset();
  start();
  LittleFS.storage[LOG_CURRENT].clear();
  Serial.input = "LOG READY\n";
  until_done();
  failed_with("read_error");

  reset();
  LittleFS.fail_write_open = true;
  start();
  until_done();
  assert(Serial.output.find("@@DIAG_ERROR cannot_flush_log\n") != std::string::npos);
  assert(Serial.output.find("@@DIAG_BEGIN") == std::string::npos);
  assert(_buffered > 0);
  LittleFS.fail_write_open = false;
  assert(diagnostics_flush());
  failed_with("cannot_flush_log");

  reset();
  assert(serial_output_begin_export());
  diagnostics_export_begin("/absent_previous", "/absent_current", millis());
  until_done();
  assert(Serial.output.find("@@DIAG_ERROR cannot_open_log\n") != std::string::npos);
  failed_with("cannot_open_log");

  reset();
  downloader();
  start();
  // Stop mid-payload, then disconnect: no further read or ACK can block cleanup.
  Serial.write_limit = 7;
  while (phase != Phase::Payload) poll();
  Serial.connected = false;
  until_done();
  Serial.on_write = {};
  failed_with("usb_disconnected");

  reset();
  start();
  Serial.backpressured = true;
  diagnostics_log("buffered_before_fatal");
  try {
    diagnostics_display_timeout(1, 15000);
    assert(false);
  } catch (const MockRestart &) {
    assert(!diagnostics_export_active());
    assert(!previous_file && !current_file);
    assert(stored().find("display_busy_timeout gpio=1 elapsed=15000 action=restart")
           != std::string::npos);
    assert(stored().find("buffered_before_fatal") != std::string::npos);
    failed_with("display_timeout");
  }
}

static void test_backpressure_and_parser() {
  reset();
  Serial.input = std::string(100, 'X') + "\n";
  poll();
  assert(!diagnostics_export_active());
  poll();
  assert(Serial.output.find("Unknown command") != std::string::npos);
  Serial.output.clear();
  downloader();
  start();
  Serial.backpressured = true;
  poll(100);
  assert(Serial.output.empty() && diagnostics_export_active());
  Serial.backpressured = false;
  until_done();
  Serial.on_write = {};
  assert(stored().find("EXPORT_END") != std::string::npos);

  reset();
  start();
  poll();
  assert(phase == Phase::Ready);
  for (char c : std::string("LOG READY\r\n")) {
    Serial.input += c;
    poll(10);
  }
  assert(phase == Phase::Ack);
  Serial.input = "LOG A";
  poll();
  assert(phase == Phase::Ack);
  now_ms += 1999;
  poll();
  failed_with("progress_timeout"); // Partial ACK bytes cannot extend the deadline.

  reset();
  Serial.input = "LOG DUMP\nLOG READY\nWRONG ACK\n";
  poll();
  assert(Serial.input == "LOG READY\nWRONG ACK\n");
  until_done();
  failed_with("invalid_ack");
}

int main() {
  assert(!diagnostics_log("not_initialized"));
  assert(reference_crc("123456789") == 0xcbf43926UL);
  test_recording();
  test_transfer(false, false);
  test_transfer(true, false);
  test_transfer(true, true);
  test_failures();
  test_backpressure_and_parser();
  puts("Incremental diagnostics snapshot, CRC, bounded work, deadlines and cancellation passed");
}
