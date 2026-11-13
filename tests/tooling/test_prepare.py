"""Synthetic fixtures only; no game instructions or assets are stored here."""
import hashlib
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
import kirby


def fixture():
    data = bytearray(0x200)
    for offset, value in [(0, 0x100), (0x48, 0x80004000), (0x90, 0x100),
                          (0xD8, 0x80008000), (0xDC, 0x20), (0xE0, 0x80004000)]:
        struct.pack_into(">I", data, offset, value)
    # Synthetic branch-and-link, signed addi pair and unsigned ori pair.
    words = {0x100: 0x48000021, 0x120: 0x3DA08090, 0x124: 0x39AD8120,
             0x128: 0x3C40808F, 0x12C: 0x6042F100, 0x130: 0x4E800020}
    for offset, word in words.items():
        struct.pack_into(">I", data, offset, word)
    return bytes(data)


class PreparationTests(unittest.TestCase):
    def test_signed_addi_and_ori_bases(self):
        dol = kirby.Dol(fixture())
        self.assertEqual(dol.first_call(), 0x80004020)
        bases, evidence = dol.sda_bases(0x80004020, 0x14)
        self.assertEqual(bases, {13: 0x808F8120, 2: 0x808FF100})
        self.assertEqual(len(evidence), 2)

    def test_missing_base_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "r2 and r13"):
            kirby.Dol(fixture()).sda_bases(0x80004000, 0x10)

    def test_initial_stack_pair_is_read_separately_from_sda(self):
        data = bytearray(fixture())
        struct.pack_into(">II", data, 0x130, 0x3C2080A0, 0x60211200)
        registers, evidence = kirby.Dol(data).sda_bases(0x80004020, 0x18, (1,))
        self.assertEqual(registers, {1: 0x80A01200})
        self.assertEqual(evidence[0]["address"], "0x80004030")

    def test_missing_initial_stack_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "r1 from initialization"):
            kirby.Dol(fixture()).sda_bases(0x80004020, 0x14, (1,))

    def test_truncated_section_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "file bounds"):
            kirby.Dol(fixture()[:-4])

    def test_out_of_mem1_is_rejected(self):
        data = bytearray(fixture())
        struct.pack_into(">I", data, 0x48, 0x90000000)
        with self.assertRaisesRegex(ValueError, "MEM1"):
            kirby.Dol(data)

    def test_wrong_revision_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "SHA-1 mismatch"):
            kirby.verify_hash(fixture(), kirby.EXPECTED_SHA1)

    def test_hash_accepts_only_exact_bytes(self):
        data = fixture()
        kirby.verify_hash(data, hashlib.sha1(data).hexdigest())
        with self.assertRaisesRegex(ValueError, "SHA-1 mismatch"):
            kirby.verify_hash(data + b"x", hashlib.sha1(data).hexdigest())

    def test_function_map_excludes_data_and_preserves_names(self):
        source = """a = .text:0x80004020; // type:function size:0x10
table = .data:0x80005000; // type:object size:0x20
alias = .text:0x80004020; // type:function size:0x10
__start = .init:0x80004000; // type:function size:0x20
"""
        entries = kirby.parse_symbols(source, {".init", ".text"})
        self.assertEqual(entries, [(0x80004000, "__start", 0x20), (0x80004020, "a", 0x10)])

    def test_function_in_data_section_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "code section"):
            kirby.parse_symbols("f = .data:0x80004000; // type:function size:0x10", {".text"})


if __name__ == "__main__":
    unittest.main()
