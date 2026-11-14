# SPDX-License-Identifier: GPL-3.0-only
"""RK5E01 immediate position-matrix upload identity."""
import hashlib

FUNCTION = ("GXLoadPosMtxImm", 0x8068BD70, 0x50,
            "0ee093690c27d4a0290ca6d2b220b802a84901afe3e41f187cb0ab9a96ea79f3")


def audit(dol, symbols):
    symbol, address, size, digest = FUNCTION
    if symbols.get(symbol) != (address, size):
        raise ValueError("Position-matrix upload symbol differs from audited RK5E01 code")
    if hashlib.sha256(dol.code(address, size)).hexdigest() != digest:
        raise ValueError("Position-matrix upload bytes differ from audited RK5E01 code")
    return {symbol: address}
