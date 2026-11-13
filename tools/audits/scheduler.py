# SPDX-License-Identifier: GPL-3.0-only
"""Audited RK5E01 scheduler/context boundaries; no game bytes are embedded."""
import hashlib
import re
import struct

FUNCTIONS = {
    "AlarmInit": ("__OSInitAlarm", 0x80662A00, 0x58, "f588c12399f972ba426cee9e9bd1a0a87edf780dba7be5ad4fc53bcb421af6e3"),
    "SetAlarm": ("OSSetAlarm", 0x80662CC0, 0x70, "633ea35b7df3f1b8c5caa1231893526f69be2c7061457364315c29d24024b5b0"),
    "CancelAlarm": ("OSCancelAlarm", 0x80662DC0, 0x118, "e7257097935b3ef53f4e8900c3afa99ba64b09cb1ee93fd006610b01ffe3abbc"),
    "AlarmCallback": ("DecrementerExceptionCallback", 0x80662EE0, 0x22C, "af42812e84aff29113f67528f49dafc41464dcc928314d6da063ec7eda97ff75"),
    "SleepTicks": ("OSSleepTicks", 0x8066C1E0, 0xA8, "a197f6a526d56af401bd7f797bf392b9dff1d881fe59dd1e8991aed95c5feb65"),
    "SystemTime": ("__OSGetSystemTime", 0x8066C2C0, 0x64, "9b41533c6b1456ab391cfe35de2f9758816a3ec18d1175db997ae7b502237ef0"),
    "CreateThread": ("OSCreateThread", 0x8066B4B0, 0x26C, "d0f35eaae5f89bcd53319f74c723d09e6a801e5b459691c231f4afbda035d489"),
    "ExitThread": ("OSExitThread", 0x8066B720, 0xE4, "56901cbaee3f27c3a963e44738112210d8c58af8c6f969c40c80585e1004e42a"),
    "CancelThread": ("OSCancelThread", 0x8066B810, 0x1D4, "965c4b732244d0f47692af4fb52148de72413e8708030dc29c0b02277284f06a"),
    "ResumeThread": ("OSResumeThread", 0x8066BB40, 0x29C, "b2b47b62b7f4ac777ca76e0e2e72149cf726361abed82389e5c2bf89c6ef9388"),
    "SleepThread": ("OSSleepThread", 0x8066BF70, 0xF0, "162cbd1249915bca05631989d419211b0dcda618c026241d9e97360185bb9a9c"),
    "WakeupThread": ("OSWakeupThread", 0x8066C060, 0xF8, "5ddbcaa6fb942aafa9b51d9f94eda49f8a2cec96aa5a369dd38b6acfe3168b8c"),
    "InitContext": ("OSInitContext", 0x806645D0, 0xBC, "bd883985d040e70d4bd47711adc2a1527a4f56a72269f53a2da2b0c1f2afa176"),
    "SelectThread": ("SelectThread", 0x8066B220, 0x22C, "8f6f073a41433c07648f0d9083e5d8b8ae51bc3edb7a75b038031cc30a5b7e10"),
    "LoadContext": ("OSLoadContext", 0x80664450, 0xD8, "f916c56f5ab2a869e5ceba82991f3e3f0eefc264ee8a6f634fee825bb76d4899"),
    "SaveContext": ("OSSaveContext", 0x806643D0, 0x80, "5076f0fde2d065938e8f0b535ed2f71516e6923fe2b9c1a97ea2f853a2bcd713"),
    "SetCurrentContext": ("OSSetCurrentContext", 0x80664360, 0x5C, "574f06429fe6a36a360f8d6010c7879996b2aa5c588dcc55ff4f25973e4dd51a"),
    "ClearContext": ("OSClearContext", 0x806645A0, 0x24, "fd4542e14445e0e9a3c7f28ed65229b27fb010e1b28e61fb3bbbfdde07da9408"),
    "DispatchInterrupt": ("fn_80668650", 0x80668650, 0x2A8, "ea10fd554de3a35a5aad0698d95520275391f6db94b1208d0661c9946fdb7c15"),
    "ThreadInit": ("__OSThreadInit", 0x8066AC10, 0x284, "519e143676002f318d56efce4bb504b32d551d920bf89bf774aadbab5de3e8c2"),
    "ViInit": ("fn_80679AB0", 0x80679AB0, 0x548, "a66f8943c0efde3d4eca261521acfed292381dd0daf3a0b680dea660b3a08e13"),
    "ViInterrupt": ("fn_80678EE0", 0x80678EE0, 0x80C, "2eefab877f17206bef13f17f7207b1a6274443f765840f4c31bd1e4774892b70"),
    "ViWait": ("fn_8067A000", 0x8067A000, 0x54, "00c9a0abf311275d3cebe910d36fde2b5a44aa802072eefa9c15b209614b5bd2"),
}

OBJECTS = {
    "AlarmQueue": ("AlarmQueue", 0x808E5EC0, 8),
    "SchedulerDisableCount": ("Reschedule", 0x808E5F38, 4),
    "RunQueueHint": ("RunQueueHint", 0x808E5F3C, 4),
    "RunQueueBits": ("RunQueueBits", 0x808E5F40, 4),
    "RunQueue": ("RunQueue", 0x808FE410, 0x100),
    "IdleContext": ("IdleContext", 0x808FE510, 0x2C8),
    "SwitchThreadCallback": ("SwitchThreadCallback", 0x808E4590, 4),
}

def audit(dol, symbols, source_symbols, sda):
    result = {}
    for label, (name, address, size, digest) in FUNCTIONS.items():
        if symbols.get(name) != (address, size) or hashlib.sha256(dol.code(address, size)).hexdigest() != digest:
            raise ValueError(f"Scheduler function differs from audited RK5E01 code: {name}")
        result[label] = address
    for label, (name, address, size) in OBJECTS.items():
        pattern = rf"^{name}\s*=\s*\.[a-z0-9]+:0x{address:08X};.*type:object size:0x{size:X}\b"
        if not re.search(pattern, source_symbols, re.M | re.I):
            raise ValueError(f"Scheduler object differs from audited RK5E01 layout: {name}")
        dol.check_mem1(address, size)
        result[label] = address
    words = struct.unpack(">139I", dol.code(result["SelectThread"], 0x22C))
    refs = {sda + struct.unpack(">h", struct.pack(">H", w & 0xFFFF))[0] for w in words
            if (w >> 16) & 31 == 13 and w >> 26 in (32, 36)}
    for label in ("SchedulerDisableCount", "RunQueueHint", "RunQueueBits", "SwitchThreadCallback"):
        if result[label] not in refs:
            raise ValueError(f"SelectThread does not reference {label}")
    return result
