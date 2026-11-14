# SPDX-License-Identifier: GPL-3.0-only
"""Synthetic function extents and bytes; no instructions from a game dump."""
import hashlib
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "tools"))
from audits import matrix


class MatrixAuditTests(unittest.TestCase):
    def setUp(self):
        self.address = 0x80004000
        self.body = b"synthetic matrix identity fixture"
        self.metadata = ("GXLoadPosMtxImm", self.address, len(self.body),
                         hashlib.sha256(self.body).hexdigest())
        self.symbols = {"GXLoadPosMtxImm": (self.address, len(self.body))}

    def code(self, address, size):
        if address != self.address or size != len(self.body):
            raise ValueError("Synthetic function extent differs")
        return self.body

    def audit(self):
        with patch.object(matrix, "FUNCTION", self.metadata):
            return matrix.audit(self, self.symbols)

    def test_verified_identity_returns_only_the_audited_entry(self):
        self.assertEqual(self.audit(), {"GXLoadPosMtxImm": self.address})

    def test_changed_bytes_are_rejected(self):
        self.body = self.body[:-1] + b"!"
        with self.assertRaisesRegex(ValueError, "bytes"):
            self.audit()

    def test_changed_address_or_extent_is_rejected(self):
        for entry in ((self.address + 4, len(self.body)), (self.address, len(self.body) + 4)):
            with self.subTest(entry=entry):
                self.symbols["GXLoadPosMtxImm"] = entry
                with self.assertRaisesRegex(ValueError, "symbol"):
                    self.audit()

    def test_other_symbol_at_the_same_address_is_rejected(self):
        self.symbols = {"GXLoadNrmMtxImm": (self.address, len(self.body))}
        with self.assertRaisesRegex(ValueError, "symbol"):
            self.audit()
