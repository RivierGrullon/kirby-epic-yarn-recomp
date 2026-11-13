# SPDX-License-Identifier: GPL-3.0-only
"""RK5E01 cache boundaries checked against OSCache.c, symbols and DOL bytes."""
import hashlib

FUNCTIONS = {
    "DCInvalidateRange": ("DCInvalidateRange", 0x80663A80, 0x2C, "4f84050b94b4b2c17ad350c4a1a3eac215276b3af65d3fbc0d7b4bc615ff36a5"),
    "DCFlushRange": ("DCFlushRange", 0x80663AB0, 0x30, "93cccdef2db2d4edd330b23e345500e30ae5ead17cfaea13e3c8ac963a39ab33"),
    "DCStoreRange": ("fn_80663AE0", 0x80663AE0, 0x30, "18ef22fd876c32a67466e960df329bb94c9dadfa38bf622fc296ed027e174912"),
    "DCFlushRangeNoSync": ("DCFlushRangeNoSync", 0x80663B10, 0x2C, "609869ef211e15fd73610a3431bcdf99f1597d86ee3854f6bba65fd3b0b6d226"),
    "DCStoreRangeNoSync": ("fn_80663B40", 0x80663B40, 0x2C, "a7929eb21f752bcfe4a57ddaa40202556220e1d633448f8e100a662a807a5d10"),
}


def audit(dol, symbols):
    for label, (symbol, address, size, digest) in FUNCTIONS.items():
        if symbols.get(symbol) != (address, size) or hashlib.sha256(dol.code(address, size)).hexdigest() != digest:
            raise ValueError(f"Cache function differs from audited RK5E01 code: {label}")
    return {label: values[1] for label, values in FUNCTIONS.items()}
