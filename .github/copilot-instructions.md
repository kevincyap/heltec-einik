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

PlatformIO may not be on PATH; use the full path:
- Linux/macOS: `~/.platformio/penv/bin/pio`
- Windows: `~/.platformio/penv/Scripts/pio.exe`

## Tests

```sh
pio test -e vision-master-e213
pio test -e vision-master-e213 --filter "test_suite_name"
```

## Architecture

- **`src/main.cpp`** — Entry point. Manages app state (menu vs reading vs upload), button dispatch, deep sleep, and the RTC fast-wake path.
- **`src/reader.cpp`** — Core text engine. Loads `.txt` from LittleFS into PSRAM, paginates with word-wrapping using font glyph metrics, renders pages with partial refresh. Maintains a `.pc` page-offset cache per book in LittleFS to skip repagination on warm opens.
- **`src/menu.cpp`** — Book selection UI. Enumerates `.txt` files, renders scrollable list with inverted highlight. Includes "Upload Books" option.
- **`src/wifi_upload.cpp`** — WiFi AP mode + captive portal + ESPAsyncWebServer for wireless file upload. Embedded HTML/JS web UI with file list, upload progress bar, and delete. Calls `reader_delete_book_cache()` on file delete to keep `.pc` caches consistent.
- **`src/button.cpp`** — ISR-driven button handler with debounce. Short press = next, long press (≥400ms) = prev, very long (≥2s) = menu.
- **`src/state.cpp`** — Binary state persistence to LittleFS. Saves filename + page number, restores on boot/wake. Write-deduplicates: skips the LittleFS write if filename+page are unchanged from the in-memory cache.
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

