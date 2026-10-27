import importlib.util
from pathlib import Path
import unittest
import zlib

script = Path(__file__).resolve().parents[1] / "scripts" / "download-diagnostics.py"
spec = importlib.util.spec_from_file_location("download_diagnostics", script)
downloader = importlib.util.module_from_spec(spec)
spec.loader.exec_module(downloader)


class SerialStream:
    def __init__(self, data):
        self.data = bytearray(data)
        self.command = None
        self.commands = []

    def write(self, data):
        self.command = data
        self.commands.append(data)
        return len(data)

    def read(self, size):
        size = min(size, 3, len(self.data))
        data = bytes(self.data[:size])
        del self.data[:size]
        return data


def frame(payload, checksum=None):
    crc = zlib.crc32(payload) if checksum is None else checksum
    return (f"@@DIAG_BEGIN {len(payload)}\n".encode() + payload +
            f"\n@@DIAG_END {crc:08x}\n".encode())


class DownloadTests(unittest.TestCase):
    def test_fragmented_payload_and_serial_chatter(self):
        payload = b"[boot=12345678 ms=100] BUG_MARKER page=3\n"
        stream = SerialStream(b"Reader starting...\n" + frame(payload))
        self.assertEqual(downloader.download_log(stream), payload)
        self.assertEqual(stream.command, b"LOG DUMP\n")

    def test_rejects_bad_checksum(self):
        with self.assertRaisesRegex(RuntimeError, "checksum"):
            downloader.download_log(SerialStream(frame(b"log\n", checksum=0)))

    def test_rejects_firmware_error(self):
        with self.assertRaisesRegex(RuntimeError, "cannot_flush"):
            downloader.download_log(SerialStream(b"@@DIAG_ERROR cannot_flush_log\n"))

    def test_rejects_unreasonable_size(self):
        with self.assertRaisesRegex(RuntimeError, "large log"):
            downloader.download_log(SerialStream(b"@@DIAG_BEGIN 9999999\n"))

    def test_incomplete_payload_times_out(self):
        with self.assertRaisesRegex(RuntimeError, "Incomplete download"):
            downloader.download_log(SerialStream(b"@@DIAG_BEGIN 10\nabc"), timeout=0.02)

    def test_missing_header_times_out(self):
        with self.assertRaisesRegex(RuntimeError, "Timed out"):
            downloader.download_log(SerialStream(b""), timeout=0.02)

    def test_version2_acknowledges_each_chunk(self):
        payload = b"x" * 600
        stream = SerialStream(frame(payload).replace(b"@@DIAG_BEGIN 600\n",
                              b"@@DIAG_BEGIN2 600 256\n"))
        self.assertEqual(downloader.download_log(stream), payload)
        self.assertEqual(stream.commands,
                         [b"LOG DUMP\n", b"LOG READY\n"] + [b"LOG ACK\n"] * 3)

    def test_version2_rejects_bad_chunk_size(self):
        with self.assertRaisesRegex(RuntimeError, "chunk size"):
            downloader.download_log(SerialStream(b"@@DIAG_BEGIN2 600 0\n"))


if __name__ == "__main__":
    unittest.main()
