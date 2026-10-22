#pragma once
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#define RTC_DATA_ATTR
#define HIGH 1
#define LOW 0
#define INPUT 1
#define OUTPUT 2
#define ADC_0db 0

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

struct SerialMock {
  void begin(int) {}
  void println(const char *) {}
  template<typename... Args> void printf(const char *, Args...) {}
  void flush() {}
};
extern SerialMock Serial;
