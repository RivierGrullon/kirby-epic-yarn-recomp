"""Synthetic PPC-shaped fixtures only; no game instructions copied from a dump."""
import hashlib
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from audits import axfx


class Fixture:
    def __init__(self, wrapper=0x80004000, callback=0x80004100):
        self.wrapper, self.callback = wrapper, callback
        words = [(op << 26) | (base << 16) | offset for op, base, offset in sorted(axfx.ACCESSES)]
        words += [(14 << 26) | (30 << 21) | 3, (10 << 26) | (31 << 16) | 96]
        self.data = {wrapper: struct.pack(">I", (18 << 26) | ((callback-wrapper) & 0x03FFFFFC)),
                     callback: struct.pack(f">{len(words)}I", *words)}

    def metadata(self):
        labels = ("AxReverbHiCallback", "AxReverbHiExpCallback")
        return {label: (address, len(self.data[address]), hashlib.sha256(self.data[address]).hexdigest())
                for label, address in zip(labels, (self.wrapper, self.callback))}

    def symbols(self):
        return {f"fn_{address:08X}": (address, len(data)) for address, data in self.data.items()}

    def code(self, address, size):
        data = self.data[address]
        if len(data) != size:
            raise ValueError("synthetic function size differs")
        return data


class AxfxAuditTests(unittest.TestCase):
    def audit(self, fixture, metadata=None, symbols=None):
        with patch.object(axfx, "FUNCTIONS", fixture.metadata() if metadata is None else metadata):
            return axfx.audit(fixture, fixture.symbols() if symbols is None else symbols)

    def test_valid_boundaries_return_addresses(self):
        f = Fixture()
        self.assertEqual(self.audit(f), {"AxReverbHiCallback": f.wrapper,
                                         "AxReverbHiExpCallback": f.callback})

    def test_negative_tail_branch_is_sign_extended(self):
        self.audit(Fixture(wrapper=0x80004200, callback=0x80004000))

    def test_changed_function_bytes_are_rejected(self):
        f = Fixture()
        metadata = f.metadata()
        f.data[f.callback] = f.data[f.callback][:-1] + b"\xff"
        with self.assertRaisesRegex(ValueError, "bytes differ"):
            self.audit(f, metadata=metadata)

    def test_changed_symbol_extent_is_rejected(self):
        f = Fixture()
        symbols = f.symbols()
        symbols[f"fn_{f.callback:08X}"] = (f.callback, 4)
        with self.assertRaisesRegex(ValueError, "symbols differ"):
            self.audit(f, symbols=symbols)

    def test_non_tail_branch_is_rejected_even_with_matching_hash(self):
        f = Fixture()
        word, = struct.unpack(">I", f.data[f.wrapper])
        f.data[f.wrapper] = struct.pack(">I", word | 1)
        with self.assertRaisesRegex(ValueError, "tail branch"):
            self.audit(f)

    def test_changed_effect_layout_is_rejected_even_with_matching_hash(self):
        f = Fixture()
        f.data[f.callback] = b"\0\0\0\0" + f.data[f.callback][4:]
        with self.assertRaisesRegex(ValueError, "layout"):
            self.audit(f)

    def test_wrong_branch_target_is_rejected_even_with_matching_hash(self):
        f = Fixture()
        word, = struct.unpack(">I", f.data[f.wrapper])
        f.data[f.wrapper] = struct.pack(">I", word + 4)
        with self.assertRaisesRegex(ValueError, "tail branch"):
            self.audit(f)

    def test_changed_channel_count_is_rejected(self):
        f = Fixture()
        words = list(struct.unpack(f">{len(f.data[f.callback])//4}I", f.data[f.callback]))
        words[-2] = (14 << 26) | (30 << 21) | 2
        f.data[f.callback] = struct.pack(f">{len(words)}I", *words)
        with self.assertRaisesRegex(ValueError, "channel count"):
            self.audit(f)

    def test_changed_sample_count_is_rejected(self):
        f = Fixture()
        words = list(struct.unpack(f">{len(f.data[f.callback])//4}I", f.data[f.callback]))
        words[-1] = (10 << 26) | (31 << 16) | 48
        f.data[f.callback] = struct.pack(f">{len(words)}I", *words)
        with self.assertRaisesRegex(ValueError, "sample count"):
            self.audit(f)


if __name__ == "__main__":
    unittest.main()
