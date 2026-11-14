# SPDX-License-Identifier: GPL-3.0-only
"""Setup source validation uses synthetic disc metadata, never game assets."""
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import kirby
from tooling.test_prepare import fixture
from installer.data import inspect_data


def make_data(root):
    root.mkdir(parents=True)
    (root / "sys").mkdir()
    (root / "files/level").mkdir(parents=True)
    dol = fixture()
    (root / "sys/main.dol").write_bytes(dol)
    boot = bytearray(0x440)
    boot[:6] = b"RK5E01"
    struct.pack_into(">I", boot, 0x18, 0x5D1C9EA3)
    # Root, a directory and one four-byte file at logical disc offset 0x20000.
    fst = struct.pack(">9I", 0x01000000, 0, 3, 0x01000000, 0, 3, 6, 0x8000, 4)
    fst += b"level\0test\0\0"  # Word-aligned Wii FST size.
    for offset, value in ((0x420, 0x10000 // 4), (0x424, 0x11000 // 4), (0x428, len(fst) // 4)):
        struct.pack_into(">I", boot, offset, value)
    (root / "sys/boot.bin").write_bytes(boot)
    (root / "sys/bi2.bin").write_bytes(bytes(0x2000))
    (root / "sys/apploader.img").write_bytes(bytes(0x20))
    (root / "sys/fst.bin").write_bytes(fst)
    (root / "files/level/test").write_bytes(b"test")
    for name, size, title_offset in (("ticket.bin", 0x2A4, 0x1DC), ("tmd.bin", 0x208, 0x18C)):
        data = bytearray(size)
        struct.pack_into(">I", data, 0, 0x10001)
        struct.pack_into(">Q", data, title_offset, 0x00010000524B3545)
        if name == "tmd.bin":
            struct.pack_into(">H", data, 0x1DE, 1)
        (root / name).write_bytes(data)
    return hashlib.sha1(dol).hexdigest()


class DataInspectionTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="kirby's setup ")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "selected/DATA"
        digest = make_data(self.root)
        self.hash_patch = patch.object(kirby, "EXPECTED_SHA1", digest)
        self.hash_patch.start()
        self.addCleanup(self.hash_patch.stop)

    def test_inspects_selected_data_and_export_parent(self):
        for selected in (self.root, self.root.parent):
            result = inspect_data(selected)
            self.assertEqual(result["data_root"], str(self.root.resolve()))
            self.assertEqual(result["game_id"], "RK5E01")
            self.assertEqual(result["revision"], 0)
            self.assertEqual(result["entry_point"], 0x80004000)
            self.assertEqual(result["asset_files"], 1)
            self.assertEqual(result["asset_bytes"], 4)

    def test_never_falls_back_to_an_existing_extraction(self):
        unrelated = Path(self.temp.name) / "other"
        unrelated.mkdir()
        with self.assertRaises((OSError, ValueError)):
            inspect_data(unrelated)

    def test_rejects_changed_dol_and_revision(self):
        path = self.root / "sys/main.dol"
        original = path.read_bytes()
        path.write_bytes(path.read_bytes() + b"changed")
        with self.assertRaisesRegex(ValueError, "SHA-1 mismatch"):
            inspect_data(self.root)
        path.write_bytes(original)
        make = self.root / "sys/boot.bin"
        boot = bytearray(make.read_bytes())
        boot[7] = 1
        make.write_bytes(boot)
        with self.assertRaisesRegex(ValueError, "boot header"):
            inspect_data(self.root)

    def test_requires_each_asset_and_rejects_symlink_directories(self):
        path = self.root / "files/level/test"
        path.unlink()
        with self.assertRaises((OSError, ValueError)):
            inspect_data(self.root)
        outside = Path(self.temp.name) / "outside"
        (self.root / "files/level").rename(outside)
        (outside / "test").write_bytes(b"test")
        (self.root / "files/level").symlink_to(outside, target_is_directory=True)
        with self.assertRaises((OSError, ValueError)):
            inspect_data(self.root)

    def test_rejects_truncated_assets_and_wrong_title_metadata(self):
        path = self.root / "files/level/test"
        path.write_bytes(b"bad")
        with self.assertRaisesRegex(ValueError, "size"):
            inspect_data(self.root)
        path.write_bytes(b"test")
        ticket = bytearray((self.root / "ticket.bin").read_bytes())
        ticket[0x1DC] ^= 1
        (self.root / "ticket.bin").write_bytes(ticket)
        with self.assertRaisesRegex(ValueError, "ticket"):
            inspect_data(self.root)

    def test_rejects_fst_path_escape_and_overlapping_extents(self):
        path = self.root / "sys/fst.bin"
        original = path.read_bytes()
        path.write_bytes(original.replace(b"test\0", b"../x\0"))
        with self.assertRaisesRegex(ValueError, "name"):
            inspect_data(self.root)
        data = bytearray(original)
        struct.pack_into(">I", data, 28, 0x10000 // 4)
        path.write_bytes(data)
        with self.assertRaisesRegex(ValueError, "Overlapping"):
            inspect_data(self.root)

    def test_cancellation_stops_before_reading(self):
        def cancelled():
            raise InterruptedError("cancelled")
        with self.assertRaises(InterruptedError):
            inspect_data(self.root, check_cancel=cancelled)

    def test_legacy_extractor_cannot_accept_a_new_source_using_an_old_marker(self):
        import extract
        source = Path(self.temp.name) / "different.wbfs"
        source.write_bytes(b"not the selected game")
        marker = self.root.parent / ".extraction-complete"
        marker.write_text("old completion marker")
        with patch.object(extract, "ROOT", self.root.parent.parent), \
                patch.object(extract, "EXPECTED_SHA1", kirby.EXPECTED_SHA1), \
                patch.object(sys, "argv", ["extract.py", str(source)]):
            # Legacy layout is ROOT/disc/DATA; selected was made at ROOT/selected/DATA.
            self.root.parent.rename(self.root.parent.parent / "disc")
            self.assertEqual(extract.main(), 1)
