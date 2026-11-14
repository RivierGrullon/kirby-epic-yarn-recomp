# SPDX-License-Identifier: GPL-3.0-only
"""RK5E01 DSP task boundary and AX microcode identity; no game bytes embedded."""
import hashlib
import re
from . import axfx


def audit(dol, symbols, source_symbols):
    result = axfx.audit(dol, symbols)
    functions = {
        "DspBootTask": (0x80684770, 0x18C, "1401b03b5afe90748823effa38e330588fd2551ab411d77c6204164e6724e3e3"),
        "AxOutInitDsp": (0x8067FAE0, 0xEC, "a6069f770b85bb40e69f88d60f01650bbc2446018fb166735a430b5bfed1a45f"),
    }
    for label, (address, size, digest) in functions.items():
        if symbols.get(f"fn_{address:08X}") != (address, size) or hashlib.sha256(dol.code(address, size)).hexdigest() != digest:
            raise ValueError(f"Audio function differs from audited RK5E01 code: {label}")
        result[label] = address
    for label, address, size in (("AxMicrocode", 0x808CA200, 0x2000),
                                 ("AxVectors", 0x808E4648, 2),
                                 ("AxTask", 0x809081E0, 0x60),
                                 ("AxInitFlag", 0x808E6280, 4)):
        if not re.search(rf"^lbl_{address:08X}\s*=.*:0x{address:08X};.*type:object size:0x{size:X}\b", source_symbols, re.M):
            raise ValueError(f"Audio object differs from audited RK5E01 symbols: {label}")
        dol.check_mem1(address, size)
        result[label] = address
    def data(address, size):
        section = next(s for s in dol.sections if s['address'] <= address and address+size <= s['address']+s['size'])
        offset = section['offset']+address-section['address']
        return dol.data[offset:offset+size]
    if data(result['AxVectors'], 6).hex() != '001000372000':
        raise ValueError("AX init/resume vectors or microcode length changed")
    code = data(result['AxMicrocode'], 0x2000)
    if hashlib.sha256(code).hexdigest() != 'd6d5952c5e6c55705513d89e58ba12314db23d8758c4acfd2c75b81b7140c0ed':
        raise ValueError("AX microcode differs from audited RK5E01 image")
    return result
