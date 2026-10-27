#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include <functional>
#ifdef _WIN32
// Failed assertions must reach the runner, not wait in a Windows dialog.
static const int test_error_mode = _set_error_mode(_OUT_TO_STDERR);
#endif

#define RTC_DATA_ATTR
#define HIGH 1
#define LOW 0
#define INPUT 1
#define INPUT_PULLUP 3
#define OUTPUT 2
#define ADC_0db 0
#define IRAM_ATTR
#define CHANGE 1

typedef void (*InterruptHandler)();
extern InterruptHandler interrupt_handlers[49];
inline int digitalPinToInterrupt(int pin) { return pin; }
inline void attachInterrupt(int pin, InterruptHandler handler, int) {
  interrupt_handlers[pin] = handler;
}
inline void noInterrupts() {}
inline void interrupts() {}

void analogSetPinAttenuation(int pin, int attenuation);
uint32_t analogReadMilliVolts(int pin);

extern unsigned long now_ms;
extern int pin_modes[49];
extern int pin_levels[49];
inline unsigned long millis() { return now_ms; }
inline void delay(unsigned long ms) { now_ms += ms; }
inline void pinMode(int pin, int mode) { pin_modes[pin] = mode; }
inline int digitalRead(int pin) { return pin_levels[pin]; }
inline void digitalWrite(int pin, int value) { pin_levels[pin] = value; }
static size_t ps_malloc_calls = 0;
inline void *ps_malloc(size_t size) {
  ps_malloc_calls++;
  return std::malloc(size);
}
template<typename T> inline T constrain(T value, T low, T high) {
  return value < low ? low : value > high ? high : value;
}
template<typename T> inline T max(T a, T b) { return a > b ? a : b; }

struct SerialMock {
  uint32_t tx_timeout_ms = 100;
  bool backpressured = false;
  std::vector<std::string> lines;
  std::string input;
  std::string output;
  size_t write_limit = size_t(-1);
  bool connected = true;
  size_t write_calls = 0;
  std::string partial_line;
  std::function<void()> on_write;
  explicit operator bool() const { return connected; }
  int availableForWrite() {
    return backpressured ? 0 : (write_limit > 8192 ? 8192 : (int)write_limit);
  }
  int available() { return (int)input.size(); }
  int read() {
    if (input.empty()) return -1;
    int value = (unsigned char)input[0];
    input.erase(0, 1);
    return value;
  }
  size_t write(const uint8_t *data, size_t count) {
    write_calls++;
    if (count > write_limit) count = write_limit;
    output.append((const char *)data, count);
    partial_line.append((const char *)data, count);
    size_t end;
    while ((end = partial_line.find('\n')) != std::string::npos) {
      lines.push_back(partial_line.substr(0, end + 1));
      partial_line.erase(0, end + 1);
    }
    if (on_write) on_write();
    return count;
  }
  void setTxTimeoutMs(uint32_t timeout) { tx_timeout_ms = timeout; }
  void begin(int) {}
  void println(const char *text) {
    lines.push_back(text);
    if (backpressured) now_ms += 20 * tx_timeout_ms;
  }
  template<typename... Args> void printf(const char *format, Args... args) {
    char text[256];
    snprintf(text, sizeof(text), format, args...);
    lines.push_back(text);
    if (backpressured) now_ms += 20 * tx_timeout_ms;
  }
  void flush() {}
};
extern SerialMock Serial;
