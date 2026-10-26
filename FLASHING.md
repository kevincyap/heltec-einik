# Flashing the Heltec Vision Master E213 (Windows)

Run these commands in **PowerShell** from this project. They use the existing
PlatformIO installation supplied by the VS Code PlatformIO extension.
This guide is for the Heltec, not the CrowPanel.

## 1. Connect and build

Use a USB **data** cable. Close any serial monitor before flashing.

```powershell
Set-Location C:\repos\heltec-einik
$pioHome = Join-Path $env:USERPROFILE '.platformio'
$pio = Join-Path $pioHome 'penv\Scripts\platformio.exe'
$python = Join-Path $pioHome 'penv\Scripts\python.exe'
$esptool = Join-Path $pioHome 'packages\tool-esptoolpy\esptool.py'

& $pio device list
& $pio run -e vision-master-e213
```

Note the device's COM port (currently `COM3`). Wait for `[SUCCESS]` before
continuing. The first build can take much longer than subsequent builds.
Building includes your current [configuration](include/config.h) and, if present,
your gitignored local Wi-Fi settings. Rebuild after changing either.

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

Update `BATTERY_DIVIDER_RATIO` in [include/config.h](include/config.h), then
rebuild and flash. Serial `[BAT]` messages also show the filtered ADC-pin
millivolts before scaling. If the display reads zero or the ADC is saturated,
changing the ratio will not fix it: check the battery connection, ADC enable,
pin selection and attenuation first. Avoid shorting battery terminals.

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
