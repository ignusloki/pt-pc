"""Synthetic setup resources, signed trailers and absolute range-update offsets; no downloads or game files."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/ci"))
from payload_index import payload_index
import release_signing
import signed_trailer


class PayloadIndexTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name)
        self.files = {"pt": b"runtime fixture", "voice/model.bin": b"model fixture"}

    def tearDown(self):
        self.temp.cleanup()

    def payload(self, files=None):
        files = self.files if files is None else files
        result = b"PTSETUP1" + struct.pack("<I", len(files))
        for name, data in files.items():
            encoded, packed = name.encode(), zlib.compress(data)
            result += struct.pack("<H", len(encoded)) + encoded + struct.pack("<QQ", len(data), len(packed))
            result += hashlib.sha256(data).hexdigest().encode() + packed
        return result

    def linux(self):
        payload = self.payload()
        setup = self.root / "Setup-linux"
        setup.write_bytes(b"ELF fixture" + payload + b"PTPAYLD1" + struct.pack("<QQ", 11, len(payload)))
        return setup

    def windows(self):
        payload = self.payload()
        image = bytearray(512 + 88)
        image[:2] = b"MZ"
        struct.pack_into("<I", image, 0x3C, 128)
        image[128:132] = b"PE\0\0"
        struct.pack_into("<H", image, 134, 1)
        struct.pack_into("<H", image, 148, 240)
        struct.pack_into("<H", image, 152, 0x20B)
        struct.pack_into("<II", image, 152 + 112 + 16, 0x1000, 88 + len(payload))
        struct.pack_into("<IIII", image, 392 + 8, 88 + len(payload), 0x1000, 88 + len(payload), 512)
        for offset, name, target in [(0, 10, 0x80000018), (24, 100, 0x80000030), (48, 0, 72)]:
            struct.pack_into("<HH", image, 512 + offset + 12, 0, 1)
            struct.pack_into("<II", image, 512 + offset + 16, name, target)
        struct.pack_into("<II", image, 512 + 72, 0x1058, len(payload))
        setup = self.root / "Setup.exe"
        setup.write_bytes(image + payload)
        return setup

    def check_ranges(self, setup):
        image = setup.read_bytes()
        entries = payload_index(setup)
        self.assertEqual({entry["path"] for entry in entries}, set(self.files))
        for entry in entries:
            data = zlib.decompress(image[entry["offset"]:entry["offset"] + entry["packed"]])
            self.assertEqual(data, self.files[entry["path"]])
            self.assertEqual(len(data), entry["size"])
            self.assertEqual(hashlib.sha256(data).hexdigest(), entry["sha256"])

    def test_linux_and_windows_absolute_ranges(self):
        self.check_ranges(self.linux())
        self.check_ranges(self.windows())

    def test_raw_payload_and_signed_setups(self):
        raw = self.root / "payload.bin"
        raw.write_bytes(self.payload())
        self.check_ranges(raw)
        for setup in (self.linux(), self.windows()):
            before = payload_index(setup)
            signed_trailer.embed(setup, b'{"test":true}', b"x" * 64)
            self.assertEqual(payload_index(setup), before)
            self.check_ranges(setup)

    def test_signed_manifest_matches_payload_listing(self):
        setup = self.linux()
        listing = self.root / "payload-manifest.json"
        listing.write_text(json.dumps({name: {"bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
                                       for name, data in self.files.items()}))
        manifest = json.loads(release_signing.manifest_bytes("1.0.8", {"linux": setup}, {"linux": listing}))
        self.assertEqual(manifest["files"]["linux"], payload_index(setup))
        self.assertEqual(manifest["packages"]["linux"]["size"], setup.stat().st_size)

    def test_truncation_paths_and_duplicate_entries_are_refused(self):
        broken = self.root / "bad.bin"
        for data in [self.payload()[:-1], self.payload({"../save": b"x"}), self.payload({"a": b"x", "A": b"y"}),
                     b"PTSETUP1" + struct.pack("<I", 0xFFFFFFFF)]:
            broken.write_bytes(data)
            with self.assertRaises(ValueError):
                payload_index(broken)
        broken.write_bytes(self.windows().read_bytes()[:-1])
        with self.assertRaises(ValueError):
            payload_index(broken)


if __name__ == "__main__":
    unittest.main()
