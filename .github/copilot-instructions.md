# Copilot Instructions

## Project Overview

PlatformIO Arduino project targeting the **Heltec Vision Master E213** (ESP32-S3, 2.13" 250×122 B&W e-ink). This is a mini e-ink text reader that displays `.txt` files stored on LittleFS, with single-button navigation and deep sleep support.

Uses `heltec-eink-modules` (Adafruit-GFX based) for display rendering. The board has one USER button on GPIO 21.

## Build & Upload

```sh
# Build firmware
pio run -e vision-master-e213

# Upload firmware to board
pio run -e vision-master-e213 --target upload

# Upload books (data/ directory) to LittleFS
pio run -e vision-master-e213 --target uploadfs

# Serial monitor (115200 baud)
pio device monitor

# Clean build artifacts
pio run -e vision-master-e213 --target clean
```

PlatformIO may not be on PATH; use the full path at `~/.platformio/penv/Scripts/pio.exe` if needed.

## Tests

```sh
pio test -e vision-master-e213
pio test -e vision-master-e213 --filter "test_suite_name"
```

## Architecture

- **`src/main.cpp`** — Entry point. Manages app state (menu vs reading), button dispatch, and deep sleep.
- **`src/reader.cpp`** — Core text engine. Loads `.txt` from LittleFS into PSRAM, paginates with word-wrapping using font glyph metrics, renders pages with partial refresh.
- **`src/menu.cpp`** — Book selection UI. Enumerates `.txt` files, renders scrollable list with inverted highlight.
- **`src/button.cpp`** — Polling-based button handler with debounce. Short press (<600ms) = next, long press (≥600ms) = prev, very long (≥2s) = menu.
- **`src/state.cpp`** — Binary state persistence to LittleFS. Saves filename + page offset, restores on boot/wake.
- **`include/config.h`** — All tuneable constants (pins, timings, layout margins, refresh interval).

## Key Conventions

- **C++11 only** — the ESP32 Arduino toolchain (GCC 8.4) does not support C++14+. Do not use default member initializers in structs that need aggregate initialization.
- **No PROGMEM** — ESP32 has unified flash/RAM address space; PROGMEM is a no-op. Use plain `const` arrays.
- **No `#define BLACK`/`WHITE`** — the `heltec-eink-modules` library defines `BLACK`, `WHITE`, `RED` as `enum Color` members. Defining them as macros in config.h will break the library. Use the enum values directly.
- **Font includes after library** — `#include <Fonts/FreeSans9pt7b.h>` must come after `#include <heltec-eink-modules.h>` (or after including a header that pulls it in) because the font headers depend on `GFXglyph`/`GFXfont` types from the library's GFX root.
- **`display.write()` is protected** — use `display.print()` for text output, not `display.write()`.
- Display type is configured via `DISPLAY_TYPE` macro in `config.h`. Change to `EInkDisplay_VisionMasterE213V1_1` for V1.1 boards.
- Text measurement uses raw GFXfont glyph `xAdvance` values (not `getTextBounds`) for pagination speed.
- Pagination and rendering use identical word-wrap logic to ensure page boundaries align exactly.
- `partitions.csv` provides a custom flash layout: 3MB app + ~5MB LittleFS data.
- Build defines `ARDUINO_USB_CDC_ON_BOOT=1` (USB serial) and `Vision_Master_E213` (board variant `#ifdef` guard).
- Books go in `data/` and are uploaded as a LittleFS image via `pio run -t uploadfs`.

