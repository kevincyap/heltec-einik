#pragma once

// =============================================================================
// Display abstraction
// -----------------------------------------------------------------------------
// Presents a single `EinkDisplay` type with a uniform, heltec-eink-modules style
// API (clearMemory / fillRect / fastmodeOn / fastmodeOff / update) regardless of
// the underlying panel driver, so reader / menu / wifi_upload code is shared
// across boards.
//
//   BOARD_VISION_MASTER_E213 -> heltec-eink-modules (LCMEN2R13EFC1, all-in-one)
//   BOARD_CROWPANEL_E213     -> GxEPD2 (SSD1680 122x250, explicit pins)
//
// Both backends derive from an Adafruit-GFX compatible root, so all GFX text
// methods (setFont/setCursor/print/getTextBounds/setTextColor/...) are common.
// =============================================================================

#include "config.h"

// -----------------------------------------------------------------------------
// Heltec Vision Master E213 (heltec-eink-modules)
// -----------------------------------------------------------------------------
#if defined(BOARD_VISION_MASTER_E213)

  #include <heltec-eink-modules.h>

  // Rotation used while reading (heltec rotation enum)
  #define DISPLAY_READ_ROTATION USB_LEFT

  // The heltec panel class already exposes clearMemory/fillRect/fastmodeOn/
  // fastmodeOff/update and inits itself in its constructor; we only add the
  // uniform lifecycle hooks used by main.cpp.
  class EinkDisplay : public DISPLAY_TYPE {
   public:
    void begin() {}            // heltec initialises in the constructor
    void prepareSleep() {}     // nothing extra required before deep sleep
  };

// -----------------------------------------------------------------------------
// Elecrow CrowPanel 2.13" (GxEPD2, SSD1680)
// -----------------------------------------------------------------------------
#elif defined(BOARD_CROWPANEL_E213)

  #include <SPI.h>
  #include <GxEPD2_BW.h>

  // GxEPD2 colours as the names the shared code expects
  #ifndef BLACK
    #define BLACK GxEPD_BLACK
  #endif
  #ifndef WHITE
    #define WHITE GxEPD_WHITE
  #endif

  // Rotation used while reading (landscape, USB on the left)
  #define DISPLAY_READ_ROTATION 1

  // SSD1680 122x250, full-height frame buffer (fits easily in ESP32-S3 RAM).
  using CrowPanelBase = GxEPD2_BW<GxEPD2_213_BN, GxEPD2_213_BN::HEIGHT>;

  class EinkDisplay : public CrowPanelBase {
   public:
    EinkDisplay()
        : CrowPanelBase(GxEPD2_213_BN(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY)) {}

    void begin() {
      // Power the panel, then route SPI to the board's display pins BEFORE
      // init() — GxEPD2's init() calls SPI.begin() internally, which no-ops if
      // the bus is already configured, preserving these custom pins.
      pinMode(EPD_PWR, OUTPUT);
      digitalWrite(EPD_PWR, HIGH);
      SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
      init(115200);
      // Establish a known, clean white baseline so the first partial refresh
      // has a correct differential reference (avoids cold-boot ghosting).
      clearScreen();
    }

    // Begin a fresh full-screen white frame. GFX draw calls write to the buffer.
    void clearMemory() {
      setFullWindow();
      fillScreen(GxEPD_WHITE);
    }

    void fastmodeOn()  { _partial = true; }
    void fastmodeOff() { _partial = false; }

    // Flush the buffer to the panel: partial = fast (no flash), full = flashing.
    void update() { display(_partial); }

    void prepareSleep() { hibernate(); }

   private:
    bool _partial = true;
  };

#endif
