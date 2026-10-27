# Flashing the Heltec Vision Master E213 (Windows)

Run these commands in **PowerShell** from this project. They use the existing
PlatformIO installation supplied by the VS Code PlatformIO extension.
This guide is for the Heltec, not the CrowPanel.

## 1. Connect and build

Use a USB **data** cable. Close any serial monitor before flashing.

```powershell
Set-Location C:\repos\heltec-einik
$pioHome = Join-Path $env:USERPROFILE '.platformio'
$pio =you C Join-Path $pioHome 'penv\Scripts\platformio.exe'
$python = Join-Path $pioHome 'penv\Scripts\python.exe'
$esptool = Join-Path $pioHome 'packages\tool-esptoolpy\esptool.py'

& $pio device list
& $pio run -e vision-master-e213
```

Note the device's COM port (currently `COM3`). Wait for `[SUCCESS]` before
continuing. The first build can take much longer than subsequent builds.
Building includes your current [configuration](include/config.h) and, if present,
your gitignored local Wi-Fi settings. Rebuild after changing either.
[Board configuration](include/board_config.h) contains pins, display selection,
battery calibration and board-specific wake behavior.
[Application configuration](include/app_config.h) contains shared layout and
timings. The existing configuration header includes both, but Wi-Fi settings are
included only by the upload module.

## 2. Flash the built firmware

The usual PlatformIO command is:

```powershell
& $pio run -e vision-master-e213 --target upload --upload-port COM3
```

On this device, its 1200-baud reset has previously made the USB port disappear.
The following direct uploader command worked instead. You can use it immediately
after a successful build; it skips that reset and does not rebuild.
Replace `COM3` if your device uses another port.

```powershell
& $python $esptool --chip esp32s3 --port COM3 --baud 460800 `
  --before default_reset --after hard_reset write_flash -z --flash_size 8MB `
  0x0000 '.pio\build\vision-master-e213\bootloader.bin' `
  0x8000 '.pio\build\vision-master-e213\partitions.bin' `
  0xe000 "$pioHome\packages\framework-arduinoespressif32\tools\partitions\boot_app0.bin" `
  0x10000 '.pio\build\vision-master-e213\firmware.bin'
```

PowerShell's backtick must be the last character on each continued line (no
trailing spaces). Wait for `Hash of data verified.` for each image and
`Hard resetting via RTS pin...`. The device should then restart.

These commands write firmware and boot metadata, **not** the books partition.
With the current [partition layout](partitions.csv), books and reading state are
normally retained. Do not run `erase_flash` or `uploadfs` just to update firmware:
`uploadfs` replaces the filesystem with the contents of [data/](data/).
If you change the partition layout, back up your books first.

## 3. Check it started

Check the screen and try a page turn. For startup logs:

```powershell
& $pio device monitor --port COM3 --baud 115200
```

Press RESET to capture startup if needed. Look for `E-Ink Reader starting...`
and `LittleFS mounted.`. Press **Ctrl+C** to close the monitor before uploading
again. Deep sleep can make the USB port disappear while the reader is idle.

## Calibrating the battery voltage

While reading, a small lock appears between the voltage and page count before
deep sleep. On the Heltec, press the recessed side USER button to wake; face
buttons do not wake it. The lock clears on wake. Adding the lock requires one
partial display refresh before sleeping.

The reading footer shows divider-scaled voltage (for example `Bat 3.920 V`)
instead of charge percentage, even outside the old 3.1-4.5 V validity range.
This is an estimate, not a calibrated measurement. Sampling is cached for
30 seconds, or 10 seconds outside that range; turn a page after the interval
or reset to get a new reading. There is no automatic footer refresh.

Compare the displayed value with a multimeter measurement of the battery:

```text
new ratio = current BATTERY_DIVIDER_RATIO * multimeter volts / displayed volts
```

Update `BATTERY_DIVIDER_RATIO` in [include/board_config.h](include/board_config.h), then
rebuild and flash. Serial `[BAT]` messages also show the filtered ADC-pin
millivolts before scaling. If the display reads zero or the ADC is saturated,
changing the ratio will not fix it: check the battery connection, ADC enable,
pin selection and attenuation first. Avoid shorting battery terminals.

## Marking bugs and downloading diagnostic logs

While **reading**, tap the **top face button** to add a `BUG_MARKER`. It does not
turn the page or refresh the display. In the book directory it still moves up;
holding USER and tapping top still opens the directory. Face buttons cannot
mark anything while asleep: wake with USER first.

Logs record accepted button GPIO/hold/queue timings, page render start/end and
stage timings, battery samples, reading-state write durations, sleep and boot.
The marker uses the original button **release time**, even when dispatch was
delayed by a display update. Its page number is the page current when the queued
marker is processed. No book text, filenames or Wi-Fi credentials are logged.

After noticing a problem, press top, then connect USB and close any serial
monitor. Keep the device awake, check its COM port, and run from this project:

```powershell
$python = Join-Path $env:USERPROFILE '.platformio\penv\Scripts\python.exe'
& $python scripts\download-diagnostics.py --port COM3
# Or choose a new output file:
& $python scripts\download-diagnostics.py --port COM3 --output .\reader-bug.log
```

The script saves a timestamped `.log` file by default, verifies the transfer's
length and CRC32, and refuses to overwrite an existing file. Downloading does
not erase the device log or books. USB is a serial connection, not a removable
drive. Opening the port may reboot some USB/driver combinations; saved logs
survive that reboot. If the reader is busy or asleep, wake/reset it and retry.
Firmware must be running normally, not in BOOT/PRG flashing mode.
Use the updated downloader with the updated firmware: version 2 waits for host
readiness and an acknowledgment after every 256-byte chunk, rather than flooding
USB buffers. Missing acknowledgments abort the transfer after two seconds;
The transfer has a 15-second overall deadline. Export advances in bounded steps
between input and application updates, so navigation remains available and idle
sleep is deferred until the transfer finishes. Display and filesystem operations
still take time; repeated page turns can make a transfer exceed its deadline.
The downloader also supports the previous firmware's unacknowledged format.

Each line has a random boot/session ID and milliseconds since that boot, not
wall-clock time. Button and marker timestamps refer to physical release;
other records use processing time, so queued events can have earlier timestamps
than preceding lines. Compare times within the same boot ID. Search the
download for `BUG_MARKER` and inspect nearby `INPUT`, `RENDER_*` and `STATE_*`
records.

Heltec buttons retain the 50 ms minimum press duration. After a handled press,
the button must stay released (HIGH) for at least 50 ms before another press can
register. Brief release glitches cannot create a second page turn or bug marker.
The first action still occurs on release without an added confirmation delay;
real separate presses remain queued during display updates.

`LOOP_SLOW` records appear only when a main-loop pass takes at least two seconds.
They split the time into `diag` (log flush/export servicing), `input` (button
polling), `action` (navigation/render/state work or menu), and `idle` (inactivity
checking), plus `total`, event and page. The normal 25 ms idle delay is excluded.
Use adjacent `RENDER_*` and `STATE_*` records to investigate a slow `action`.
The `mode` field identifies reading, menu, upload or error servicing. Time spent
waiting for user input in the menu is no longer part of a loop pass. Normal loops
add no records or extra flushes.
Timing reports are written after the pass recovers; a permanent hang can still
prevent the report from being recorded.

Two rotating LittleFS files retain at most **128 KiB** of log content; oldest
data is discarded automatically, so download reasonably soon after a problem.
Routine records use a **2 KiB RAM buffer**, flushed about every two seconds
while the main loop is available. Render-start, marker, boot and sleep records
are explicitly flushed. Flash writes add some page-turn overhead; `RENDER_END`
includes a `log` duration for the pre-render flush.

During export, the on-disk files are frozen so the advertised length and CRC
describe a stable snapshot. New records, including markers, remain in the
bounded RAM buffer until export finishes or fails. They are not included in that
download and are not yet durable; a reset can lose them. Excess records are
counted and dropped if the buffer fills. Download again to retrieve new records
after the transfer. Display-timeout recovery cancels export before flushing the
fault and restarting.

If the CPU never recovers from a stall, a queued marker cannot be processed or
saved. A reset/power loss can lose buffered records, and full/failing storage
can prevent persistence; serial `[DIAG]` errors report those failures. Logging
helps diagnose the intermittent freeze; it is not itself a fix for it.

Debug USB messages are now best-effort: disconnected/full-buffer output is
skipped instead of blocking page turns, and debug messages are suppressed during
log exports. Dropped debug-message counts appear in `EXPORT_BEGIN`; persistent
file records have separate dropped-record accounting.

Both boards now stop waiting for a stuck display BUSY signal after 15 seconds.
The reader logs `display_busy_timeout` and restarts instead of continuing to send
commands to a busy panel. It resumes the last saved page; a page whose render
failed may need to be turned again. A permanently stuck panel can cause repeated
restarts and needs a hardware/power check. Logging the fault requires working
filesystem storage. These are targeted blocking-path fixes, not proof that every
possible source of a freeze has been eliminated.

## Book uploads and storage

The [book-storage module](src/book_storage.cpp) owns upload completion, book
deletion and web bookmark updates. Uploads stream to a hidden staging file and
replace the destination by rename only after the final chunk and a successful
write. Replacing a book invalidates its pagination cache even when its size is
unchanged; its saved position is preserved and clamped when opened.

An interrupted or failed upload leaves the previous book intact. Cleanup runs
when the request disconnects; an abandoned staging file can also be removed by
the next upload. Only one book mutation runs at a time; competing changes return
HTTP 409. Filenames must be root `.txt` paths that fit the reader's 63-byte path
limit (including `/`), without nested paths or control characters.

Write, commit and metadata failures return errors instead of claiming success.
Deletion also removes the page cache/bookmark and clears global resume state
when it refers to that book. Metadata cleanup spans multiple files, so it is
checked and retryable rather than one atomic transaction. Web bookmark pages
remain zero-based; invalid, negative or overflowing values are rejected.
State and bookmark writes use staging plus rename without changing their binary
formats.

## Troubleshooting

- **No port / port disappeared:** reconnect the cable and rerun
  `& $pio device list`; the COM number may change. If necessary, hold the
  board's BOOT/PRG button while tapping RESET, then release BOOT/PRG and retry.
  Do not assume the reading USER button is a bootloader button.
- **Port busy:** close PlatformIO's monitor and other programs using that port.
- **Cannot connect:** try bootloader mode as above or reduce `--baud` to `115200`.
- **`No module named platformio`:** repair the existing environment, then rebuild:
  `& $python -m pip install platformio`.
- **Upload succeeded but screen did not change:** an e-ink screen retains its old
  image without power. Check serial startup logs rather than the image alone.
