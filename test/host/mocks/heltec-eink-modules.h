#pragma once
#include <Arduino.h>
#include <string>

#define USB_LEFT 0
#define PIN_PCB_VEXT 18
#define PIN_DISPLAY_DC 2
#define PIN_DISPLAY_CS 5
#define PIN_DISPLAY_BUSY 1
#define PIN_DISPLAY_RST 3
#define DEFAULT_SDI 6
#define DEFAULT_CLK 4

enum Color { BLACK, WHITE };
struct GFXglyph { uint8_t xAdvance; };
struct GFXfont {
  const GFXglyph *glyph;
  uint16_t first;
  uint16_t last;
};
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
  struct Rect { int x; int y; int w; int h; Color color; };
  std::vector<std::string> printed;
  std::vector<Rect> rectangles;
  int updates = 0;
  void fastmodeOn() { fastmode_state = ON; }
  void fastmodeOff() { fastmode_state = OFF; }
  void clearMemory() { printed.clear(); rectangles.clear(); }
  void fillRect(int x, int y, int w, int h, Color color) {
    rectangles.push_back({x, y, w, h, color});
  }
  void setFont(const void *) {}
  void setTextSize(int) {}
  void setTextColor(Color) {}
  void setCursor(int, int) {}
  void print(const char *text) { printed.push_back(text); }
  void setTextWrap(bool) {}
  void getTextBounds(const char *text, int16_t x, int16_t y,
                     int16_t *bx, int16_t *by, uint16_t *w, uint16_t *h) {
    *bx = x; *by = y; *w = uint16_t(strlen(text) * 6); *h = 8;
  }
  void setRotation(int) {}
  int width() { return 250; }
  int height() { return 122; }
  void update() { updates++; }

 protected:
  virtual void wait() {}
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
