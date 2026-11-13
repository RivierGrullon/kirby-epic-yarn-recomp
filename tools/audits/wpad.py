# SPDX-License-Identifier: GPL-3.0-only
"""Audited RK5E01 WPAD ABI boundaries; source: Swiftshine/key WPAD.c and DOL."""
import hashlib
import re

FUNCTIONS = {
    "WPADControlDpd": (0x806B8030, 0x1080, "a9a68920e3f6fb92683538c4741948ab459180cba4901da13c43a4f27f016b09"),
    "WPADIsDpdEnabled": (0x806B7FA0, 0x44, "935a8ea9f5c5dc88b28601acf6c2f553117985e6fbde252bd00505fddd84c936"),

    "WPADGetInfoAsync": (0x806B5A70, 0x220, "71ec92b41fb12059b298c0329debfac8665a008d464ec2c230651b93e1232948"),
    "WPADInit": (0x806B4360, 0x70, "ef38d5b0fb98bc65413d5857c8662b5d9d923a03fc072e81b8a6f8a2254443f1"),
    "WPADGetStatus": (0x806B4410, 0x4, "298652ccd2a24934ac3d5a708d9d05a96d0a1a198317047a68dd13de2f595f80"),
    "WPADProbe": (0x806B56F0, 0x8C, "cd70b38a716f3d84f3e089d712aa8ec54d58cb2b7dd22b635a3805454ef50327"),
    "WPADRead": (0x806B6080, 0x98, "b86796532ad02ae3919e7772e314c54c11b05577ae59ae1decef9749645592bd"),
    "WPADSetSamplingCallback": (0x806B5780, 0x88, "39bd4f5109bbd4e79e93f07eb1058f602483659ee2508170abcfa52b75eed1c6"),
    "WPADSetConnectCallback": (0x806B5810, 0x88, "895666c820d29eef5b836dbca71c6af55226031457036f1870570455ff80819d"),
    "WPADSetExtensionCallback": (0x806B58A0, 0x5C, "e733222a8324c1fa721f62483c9fc28975f42a0535196a5417450a3bb63ba1b1"),
    "WPADGetDataFormat": (0x806B5900, 0x44, "54a2ec720eab0a47eab12ec3a6e379078d2cb5d00ba6b9f1e5c4513d7acfb0d5"),
    "WPADSetDataFormat": (0x806B5950, 0xC0, "c28aadba41c09bc9ec89a9b0fd2cc8dbbe38ad3ba55aee3d40e6dbccec4d45d2"),
    "WPADSetAutoSamplingBuf": (0x806B6120, 0x15C, "822983fbef6ceba3cdd9802b33cc2f65fc22905615cdedc37db9bda01d760965"),
    "WPADGetAccGravityUnit": (0x806B5520, 0xD4, "6fc21d932171c692f3f6e1568d7d6bf910fdf246803f38d80b1a7d6fcc45ed19"),
    "WPADControlMotor": (0x806B5C90, 0xCC, "bc784fdd732f6e71a3d893d257edf2cb643bb9ff48b22d0ef80e737dd1631a7c"),
    "WPADGetDpdSensitivity": (0x806B7F60, 0x34, "79ca04e6ff62fa77ec4327704e2f8d3d557e4883ab39db4d270031249a3f0568"),
    "WPADiGetMplsStatus": (0x806B92E0, 0x6C, "383f429b3191e9b62f706ed9f75c1f51f075874e26681da8a5e551b27f02bfac"),
    "WPADDisconnect": (0x806B5630, 0xC0, "9c6d3b59c7ea2eab791095f604ade7e82a5fc28ab2af8fccf6501e3ead11e4cd"),
    "WPADSetCallbackByKPAD": (0x806BB1D0, 0x8, "57ceb516277bc69b83805cbebc75eed337e71057ba3b62e27cba12cd52948c0c"),
}

def audit(dol, symbols, source_symbols):
    if not re.search(r"^_wpadUsedCallback = .sbss:0x808E6558; // type:object size:0x4\b", source_symbols, re.M):
        raise ValueError("WPAD callback flag differs from the audited layout")
    for name, (address, size, digest) in FUNCTIONS.items():
        if symbols.get(name) != (address, size) or hashlib.sha256(dol.code(address, size)).hexdigest() != digest:
            raise ValueError(f"WPAD function differs from audited RK5E01 code: {name}")
    return {name: value[0] for name, value in FUNCTIONS.items()}
