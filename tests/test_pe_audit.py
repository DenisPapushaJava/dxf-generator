"""Tests for distribution guard, including otherwise easy-to-miss delay imports."""
import pathlib
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "scripts"))
from verify_windows_exe import imports  # noqa: E402


def fixture(normal=None, delayed=None, *, rva_delay=True):
    data = bytearray(1536)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3C, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HH", data, 0x84, 0x8664, 1)
    struct.pack_into("<H", data, 0x94, 240)
    optional = 0x98
    struct.pack_into("<H", data, optional, 0x20B)
    struct.pack_into("<Q", data, optional + 24, 0x400000)
    struct.pack_into("<I", data, optional + 60, 0x200)
    struct.pack_into("<I", data, optional + 108, 16)
    struct.pack_into("<IIII", data, optional + 240 + 8, 0x400, 0x1000, 0x400, 0x200)
    if normal:
        struct.pack_into("<II", data, optional + 112 + 8, 0x1000, 40)
        struct.pack_into("<IIIII", data, 0x200, 0, 0, 0, 0x1100, 0)
        name = normal.encode("ascii") + b"\0"
        data[0x300:0x300 + len(name)] = name
    if delayed:
        struct.pack_into("<II", data, optional + 112 + 13 * 8, 0x1040, 64)
        struct.pack_into("<II", data, 0x240, int(rva_delay), 0x1180 if rva_delay else 0x401180)
        name = delayed.encode("ascii") + b"\0"
        data[0x380:0x380 + len(name)] = name
    return data


class ImportTests(unittest.TestCase):
    def parse(self, data):
        with tempfile.TemporaryDirectory() as directory:
            path = pathlib.Path(directory) / "fixture.exe"
            path.write_bytes(data)
            return imports(path)

    def test_regular_and_delayed_imports(self):
        self.assertEqual(self.parse(fixture("KERNEL32.dll", "Qt6Core.dll")),
                         ["kernel32.dll", "qt6core.dll"])

    def test_legacy_delay_virtual_address(self):
        self.assertEqual(self.parse(fixture(delayed="Qt6Gui.dll", rva_delay=False)), ["qt6gui.dll"])

    def test_truncated_pe_is_rejected(self):
        for data in (b"", b"MZ", fixture()[:0x200]):
            with self.subTest(length=len(data)), self.assertRaises(ValueError):
                self.parse(data)

    def test_unmapped_import_is_rejected(self):
        data = fixture("Qt6Core.dll")
        struct.pack_into("<I", data, 0x200 + 12, 0x9000)
        with self.assertRaises(ValueError):
            self.parse(data)

    def test_x86_is_rejected(self):
        data = fixture()
        struct.pack_into("<H", data, 0x84, 0x14C)
        with self.assertRaises(ValueError):
            self.parse(data)


if __name__ == "__main__":
    unittest.main()
