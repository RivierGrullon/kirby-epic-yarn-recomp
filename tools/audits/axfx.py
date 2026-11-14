# SPDX-License-Identifier: GPL-3.0-only
"""Identify RK5E01 AXFX callback boundaries without enabling native replacements."""
import hashlib
import struct


FUNCTIONS = {
    "AxReverbHiCallback": (0x80681250, 0x4, "96e1d0a31aaa83cd022dcbf31647d4945714c364511d1ddc50c42549138dbcb7"),
    "AxReverbHiExpCallback": (0x80681680, 0x524, "ae6d29813cb5fd7263c5086422e4477a4264bc5b10b91425100eb6f5ccf44bec"),
}

# (opcode, base register, displacement), checked against AXFXReverbHiExp.h.
ACCESSES = {(32, 4, 0x10C), (36, 4, 0x10C), (48, 4, 0x108),
            (32, 4, 0x138), (32, 4, 0x13C), (48, 4, 0x140), (48, 4, 0x144),
            (32, 3, 0), (32, 3, 4), (32, 3, 8)}


def audit(dol, symbols):
    code = {}
    for label, (address, size, digest) in FUNCTIONS.items():
        if symbols.get(f"fn_{address:08X}") != (address, size):
            raise ValueError(f"AXFX function symbols differ: {label}")
        code[label] = dol.code(address, size)
        if hashlib.sha256(code[label]).hexdigest() != digest:
            raise ValueError(f"AXFX function bytes differ: {label}")
    wrapper = FUNCTIONS["AxReverbHiCallback"][0]
    branch, = struct.unpack(">I", code["AxReverbHiCallback"])
    displacement = branch & 0x03FFFFFC
    if displacement & 0x02000000:
        displacement -= 0x04000000
    if ((branch >> 26) != 18 or (branch & 3) or
            ((wrapper + displacement) & 0xFFFFFFFF) != FUNCTIONS["AxReverbHiExpCallback"][0]):
        raise ValueError("AXFX wrapper is not a relative tail branch to the callback")
    body = code["AxReverbHiExpCallback"]
    words = struct.unpack(f">{len(body)//4}I", body)
    accesses = {(word >> 26, (word >> 16) & 31, word & 0xFFFF) for word in words}
    if not ACCESSES.issubset(accesses):
        raise ValueError("AXFX callback no longer matches the audited update/effect layout")
    if not any((word >> 26) == 14 and ((word >> 21) & 31) == 30 and
               ((word >> 16) & 31) == 0 and (word & 0xFFFF) == 3 for word in words):
        raise ValueError("AXFX callback channel count differs")
    if not any((word >> 26) == 10 and ((word >> 16) & 31) == 31 and
               (word & 0xFFFF) == 96 for word in words):
        raise ValueError("AXFX callback sample count differs")
    return {label: entry[0] for label, entry in FUNCTIONS.items()}
