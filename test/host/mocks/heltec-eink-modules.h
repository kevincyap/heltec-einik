#pragma once
#include <Arduino.h>

#define USB_LEFT 0
#define PIN_PCB_VEXT 18
#define PIN_DISPLAY_DC 2
#define PIN_DISPLAY_CS 5
#define PIN_DISPLAY_BUSY 1
#define PIN_DISPLAY_RST 3
#define DEFAULT_SDI 6
#define DEFAULT_CLK 4

enum Color { BLACK, WHITE };
enum Fastmode { OFF, ON };
struct SPISettings {};
struct SPIClass {
  int transactions = 0;
  int completed = 0;
  int stopped = 0;
  std::vector<std::vector<uint8_t>> frames;
  void beginTransaction(SPISettings) { transactions++; }
  void endTransaction() { completed++; }
  void end() { stopped++; }
  void writeBytes(const uint8_t *data, uint32_t count) {
    frames.emplace_back(data, data + count);
  }
};
namespace Platform {
  inline void VExtOff() {
    pinMode(PIN_PCB_VEXT, OUTPUT);
    digitalWrite(PIN_PCB_VEXT, LOW);
  }
}

class LCMEN2R13EFC1 {
 public:
  void fastmodeOn() { fastmode_state = ON; }
  void fastmodeOff() { fastmode_state = OFF; }
  void clearMemory() {}
  void fillRect(int, int, int, int, Color) {}
  void setFont(void *) {}
  void setTextSize(int) {}
  void setTextColor(Color) {}
  void setCursor(int, int) {}
  void print(const char *) {}
  void setRotation(int) {}
  int width() { return 250; }
  int height() { return 122; }
  void update() {}

 protected:
  virtual void sendImageData() {}
  void sendCommand(uint8_t command) { commands.push_back(command); }
  bool begun = false;
  Fastmode fastmode_state = OFF;
  bool fastmode_secondpass = false;
  uint16_t page_bytecount = 4000;
  uint8_t buffer[4000] = {};
  uint8_t *page_black = buffer;
  SPIClass spi;
  SPIClass *display_spi = &spi;
  SPISettings spi_settings;
  std::vector<uint8_t> commands;
};
#define EInkDisplay_VisionMasterE213 LCMEN2R13EFC1
