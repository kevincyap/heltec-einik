# Copilot Instructions

## Project Overview

PlatformIO Arduino project for a mini e-ink text reader that displays `.txt` files stored on LittleFS, with button navigation and deep sleep support. It supports **two boards** (one PlatformIO env each), both ESP32-S3 with a 2.13" 250×122 B&W e-ink panel:

- **Heltec Vision Master E213** (env `vision-master-e213`) — panel `LCMEN2R13EFC1`, driven by `heltec-eink-modules`. Single USER button on GPIO 21. All-in-one board with fixed display pins, VEXT power control, and an onboard battery divider.
- **Elecrow CrowPanel 2.13"** (env `crowpanel-e213`, SKU DIE01021S) — panel SSD1680Z/JD79661, driven by `GxEPD2` (`GxEPD2_213_BN`). Discrete keys + rotary dial. Display pins: SCK=12, MOSI=11, RST=10, DC=13, CS=14, BUSY=9; panel power enable on GPIO 7 (drive HIGH).

Both display libraries are Adafruit-GFX based, so the text/pagination code is shared. A thin abstraction (`include/display.h`) exposes a single `EinkDisplay` type with a uniform heltec-style API (`clearMemory` / `fillRect` / `fastmodeOn` / `fastmodeOff` / `update` / `begin` / `prepareSleep`). The active board is chosen by a `BOARD_*` macro in `platformio.ini` build_flags, resolved in `include/config.h`.

## Build & Upload

```sh
# Build firmware (pick the env for your board)
pio run -e vision-master-e213      # Heltec Vision Master E213
pio run -e crowpanel-e213          # Elecrow CrowPanel 2.13"

# Upload firmware to board
pio run -e vision-master-e213 --target upload

# Upload books (data/ directory) to LittleFS
pio run -e vision-master-e213 --target uploadfs

# Serial monitor (115200 baud)
pio device monitor

# Clean build artifacts
pio run -e vision-master-e213 --target clean
```

PlatformIO may not be on PATH; use the full path:
- Linux/macOS: `~/.platformio/penv/bin/pio`
- Windows: `~/.platformio/penv/Scripts/pio.exe`

## Tests

```sh
pio test -e vision-master-e213
pio test -e vision-master-e213 --filter "test_suite_name"
```

## Architecture

- **`include/display.h`** — Board display abstraction. Defines `EinkDisplay` (subclass of the active backend) plus `DISPLAY_READ_ROTATION`. Heltec branch is a thin pass-through; CrowPanel branch wraps `GxEPD2_BW` and implements the heltec-style lifecycle API (`clearMemory`=`setFullWindow`+`fillScreen(WHITE)`, `update`=`display(_partial)`, `fastmodeOn/Off` toggles the partial flag, `begin` powers the panel + sets SPI pins + `clearScreen`, `prepareSleep`=`hibernate`).
- **`src/main.cpp`** — Entry point. Manages app state (menu vs reading vs upload), button dispatch, deep sleep, and the RTC fast-wake path. Calls `display.begin()` once in setup. Deep-sleep wake source is board-abstracted via `input_enable_deep_sleep_wake()` / `input_woke_from_button()` / `input_wait_wake_release()` (in `button.cpp`).
- **`src/reader.cpp`** — Core text engine. Loads `.txt` from LittleFS into PSRAM, paginates with word-wrapping using font glyph metrics, renders pages with partial refresh. Maintains a `.pc` page-offset cache per book in LittleFS to skip repagination on warm opens.
- **`src/menu.cpp`** — Book selection UI. Enumerates `.txt` files, renders scrollable list with inverted highlight. Includes "Upload Books" option.
- **`src/wifi_upload.cpp`** — WiFi AP mode + captive portal + ESPAsyncWebServer for wireless file upload. Embedded HTML/JS web UI with file list, upload progress bar, and delete. Calls `reader_delete_book_cache()` on file delete to keep `.pc` caches consistent.
- **`src/button.cpp`** — Input handler, board-selected via `INPUT_SINGLE_BUTTON`. Heltec: ISR-driven single button (short=next, long ≥400ms=prev, very long ≥2s=menu). CrowPanel: polled edge-detection over discrete keys — NEXT→next, PREV/OK→prev/select, HOME/EXIT→menu — emitting the same `ButtonEvent`s.
- **`src/state.cpp`** — Binary state persistence to LittleFS. Saves filename + page number, restores on boot/wake. Write-deduplicates: skips the LittleFS write if filename+page are unchanged from the in-memory cache.
- **`include/config.h`** — All tuneable constants (pins, timings, layout margins, refresh interval).

## Key Conventions

- **C++11 only** — the ESP32 Arduino toolchain (GCC 8.4) does not support C++14+. Do not use default member initializers in structs that need aggregate initialization.
- **No PROGMEM** — ESP32 has unified flash/RAM address space; PROGMEM is a no-op. Use plain `const` arrays.
- **Board-specific code goes through the abstraction** — render code calls `EinkDisplay` methods (from `include/display.h`) and shared `BLACK`/`WHITE` constants, never a backend library type directly. Add new board differences behind `BOARD_*` macros in `config.h`/`display.h`, not inline in reader/menu/wifi_upload.
- **`BLACK`/`WHITE` are board-dependent** — on Heltec they are `enum Color` members from `heltec-eink-modules` (do NOT `#define` them, it breaks the library). On CrowPanel, `display.h` `#define`s them to `GxEPD_BLACK`/`GxEPD_WHITE`. Either way, just use `BLACK`/`WHITE` in shared code.
- **Font includes after the display header** — `#include <Fonts/FreeSans9pt7b.h>` must come after `#include "display.h"` (or a header that pulls it in) because the font headers depend on `GFXglyph`/`GFXfont` types from the active library's GFX root (heltec GFX_Root or Adafruit_GFX).
- **`display.write()` is protected** — use `display.print()` for text output, not `display.write()`.
- Heltec panel revision is set via `DISPLAY_TYPE` in `config.h` — change to `EInkDisplay_VisionMasterE213V1_1` for V1.1 boards.
- Text measurement uses raw GFXfont glyph `xAdvance` values (not `getTextBounds`) for pagination speed.
- Pagination and rendering use identical word-wrap logic to ensure page boundaries align exactly.
- `partitions.csv` provides a custom flash layout: 3MB app + ~5MB LittleFS data (fits both boards' 8MB flash).
- Both envs build define `ARDUINO_USB_CDC_ON_BOOT=1` (USB serial) plus a `BOARD_*` guard; the Heltec env additionally defines `Vision_Master_E213` for the library.
- Books go in `data/` and are uploaded as a LittleFS image via `pio run -t uploadfs`.
- **CrowPanel partial-refresh tuning is unverified on hardware** — the GxEPD2 mapping (full-buffer draw + `display(true)` for fast refresh, `clearScreen()` baseline on boot) compiles and follows GxEPD2 conventions, but the two-render wake trick and `PARTIAL_CLEANUP_EXTRA_UPDATES` may need on-device tuning. `SLEEP_AFTER_PAGE_TURN` is disabled for CrowPanel (GxEPD2 deep-sleep wake forces a full refresh).

## E-Ink Partial Refresh — Key Rules

E-ink partial (fast-mode) refresh is a **differential** update: the driver computes what changed since the last frame and only drives those pixels. This means:

- **Driver state must be continuous** — partial refresh only works correctly when the driver's internal "previous frame" buffer matches what is physically on the panel. After a cold boot or deep sleep, the driver buffer resets but the panel still shows the old image. Doing a partial refresh from a mismatched state causes ghosting/overlay.
- **The two-render wake trick** — On after-turn wake, always render the *saved page* first (syncs driver buffer to panel state), then call `reader_next/prev_page()` + render again. The second render is a clean partial from a known baseline.
- **Always use `display.fastmodeOn()` before `display.update()`** for partial refresh. Never call `display.fastmodeOff()` (full refresh) during reading or menu navigation — there is no longer any periodic full-refresh cadence in this project.
- **`PARTIAL_CLEANUP_EXTRA_UPDATES`** in `config.h` controls how many additional `display.update()` calls follow each partial render (helps clear residual ghosting without a full refresh).
- **`display.clearMemory()` + `display.fillRect(WHITE)`** must always be called before drawing text, even for partial updates — this sets the new-frame buffer to blank so the differential engine clears old pixels correctly.

## Deep Sleep / Wake Architecture

This project uses `SLEEP_REASON_AFTER_TURN` to sleep immediately after a page turn and wake on the next button press to advance again. The wake path is optimised in two layers:

**Layer 1 — Two-render sync (always active):**
On after-turn wake `reader_open()` opens at the saved page, renders it (syncs driver state to real panel), then turns and renders the target page via partial refresh.

**Layer 2 — RTC fast-wake (skips all LittleFS reads on hot path):**
At sleep time, `enter_sleep()` saves a 4-entry page-offset window around the current page plus metadata into `RTC_DATA_ATTR` variables (92 bytes total, retained across deep sleep). On wake, `reader_open_fast()` restores internal reader state entirely from those RTC values — no `state_load()`, no `load_page_cache()`, no LittleFS opens before text rendering. Falls back to normal resume if RTC data is stale or invalid.

The window stores offsets for `[page-1, page, page+1, page+2]`, covering both short-press (next) and long-press (prev) directions and both renders.

Remaining unavoidable wake latency: boot ROM (~300 ms), `LittleFS.begin()` (~30 ms), two partial display updates (~400–800 ms).

## Page-Offset Cache (`.pc` files)

Paginating a large book (e.g. Moby Dick, ~1.2 MB) requires a full single-pass tokeniser run and takes noticeable time on cold boot. After the first open, pagination results are cached:

- Cache file path: same as book but with `.pc` extension (e.g. `/mobydick.pc`).
- Cache format: fixed-size header (`EPIC` magic + file_size + disp_w + disp_h + num_pages) followed by the raw `int[]` offset array.
- Cache is validated against current file_size and display dimensions — automatically invalidated if the book is re-uploaded or the layout changes.
- `reader_delete_book_cache(filename)` removes the `.pc` file; called automatically by `wifi_upload.cpp` on file delete.
- On a cache hit, `reader_open()` skips `paginate()` entirely and reads the offset array in a single bulk read.

## LittleFS Wear Levelling

LittleFS uses copy-on-write semantics — it never overwrites a block in place. New data is written to a fresh physical block, metadata is atomically updated, then the old block is freed. Block selection actively prefers less-erased sectors. This means `state.dat` page-turn writes and `.pc` cache files are automatically spread across all available sectors without manual management. The ESP32-S3 internal flash has a typical endurance of 100,000 erase cycles per sector; with a 5 MB partition (~1,250 sectors) even frequent writes take years to approach wear-out.

## Performance Notes

- Avoid `String` for filename/extension checks in hot paths (menu scan, file listing, upload handler) — use C-string helpers with direct `strlen`/`strrchr` comparisons instead.
- `state_save()` is write-deduplicated: it caches the last written filename+page in RAM and skips the LittleFS write if they haven't changed.
- The battery footer in `reader_render()` is entirely compiled out when `BATTERY_INDICATOR_ENABLED` is 0 in `config.h`.

