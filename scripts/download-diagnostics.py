import argparse
from datetime import datetime
from pathlib import Path
import re
import time
import zlib

import serial


def read_line(port, deadline):
    line = bytearray()
    while time.monotonic() < deadline:
        byte = port.read(1)
        if not byte:
            continue
        line.extend(byte)
        if len(line) > 1024:
            raise RuntimeError("Device sent an oversized protocol line")
        if byte == b"\n":
            return bytes(line).strip()
    raise RuntimeError("Timed out waiting for the device")


def download_log(port, timeout=30):
    deadline = time.monotonic() + timeout
    command = b"LOG DUMP\n"
    if port.write(command) != len(command):
        raise RuntimeError("Could not send the download command")
    while True:
        line = read_line(port, deadline)
        if line.startswith(b"@@DIAG_ERROR"):
            raise RuntimeError(line.decode("ascii", errors="replace"))
        match = re.fullmatch(rb"@@DIAG_BEGIN(2?) (\d+)(?: (\d+))?", line)
        if match:
            version2 = match.group(1) == b"2"
            size = int(match.group(2))
            if size > 1024 * 1024:
                raise RuntimeError("Device advertised an unexpectedly large log")
            chunk_size = int(match.group(3) or 0)
            if version2:
                if chunk_size != 256:
                    raise RuntimeError("Unsupported diagnostic transfer chunk size")
                if port.write(b"LOG READY\n") != 10:
                    raise RuntimeError("Could not send transfer readiness acknowledgment")
            break
    data = bytearray()
    while len(data) < size and time.monotonic() < deadline:
        target = min(len(data) + (chunk_size if version2 else 4096), size)
        while len(data) < target and time.monotonic() < deadline:
            data.extend(port.read(target - len(data)))
        if len(data) == target and version2:
            if port.write(b"LOG ACK\n") != 8:
                raise RuntimeError("Could not acknowledge diagnostic data chunk")
    if len(data) != size:
        raise RuntimeError(f"Incomplete download: received {len(data)} of {size} bytes")
    trailer = read_line(port, deadline)
    while not trailer:
        trailer = read_line(port, deadline)
    match = re.fullmatch(rb"@@DIAG_END ([0-9a-fA-F]{8})", trailer)
    if not match:
        raise RuntimeError("Missing or invalid download trailer")
    if zlib.crc32(data) != int(match.group(1), 16):
        raise RuntimeError("Log checksum mismatch; retry the download")
    return bytes(data)


def main():
    parser = argparse.ArgumentParser(description="Download the reader's persistent diagnostic log")
    parser.add_argument("--port", required=True, help="Reader serial port, for example COM3")
    parser.add_argument("--output", type=Path,
                        default=Path(datetime.now().strftime("reader-diagnostics-%Y%m%d-%H%M%S.log")))
    args = parser.parse_args()
    if args.output.exists():
        parser.error(f"Refusing to overwrite {args.output}")
    try:
        with serial.Serial(port=None, baudrate=115200, timeout=0.2, write_timeout=3) as port:
            port.dtr = False
            port.rts = False
            port.port = args.port
            port.open()
            # Some USB drivers reset the ESP32 on open; let setup finish first.
            time.sleep(2)
            data = download_log(port)
        with args.output.open("xb") as output:
            output.write(data)
    except (OSError, serial.SerialException, RuntimeError) as error:
        parser.exit(1, f"Download failed: {error}\n")
    print(f"Saved {len(data)} verified bytes to {args.output.resolve()}")


if __name__ == "__main__":
    main()
