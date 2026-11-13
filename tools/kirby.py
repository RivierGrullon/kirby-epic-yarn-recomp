#!/usr/bin/env python3
"""Prepare and translate a locally supplied, unmodified RK5E01 Rev 0 DOL."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
from audits import scheduler as scheduler_audit
from audits import wpad as wpad_audit
from audits import audio as audio_audit
from audits import cache as cache_audit

ROOT = Path(__file__).resolve().parents[1]
EXPECTED_SHA1 = "f4280a2b0709bfde4132be22e9f7fa8ecfa2a130"
MEM1_BASE, MEM1_END = 0x80000000, 0x81800000


def verify_hash(data, expected):
    actual = hashlib.sha1(data).hexdigest()
    if actual != expected:
        raise ValueError(f"SHA-1 mismatch: expected {expected}, found {actual}; unmodified RK5E01 Rev 0 required")


class Dol:
    def __init__(self, data):
        if len(data) < 0x100:
            raise ValueError("Truncated DOL header")
        self.data = data
        self.sections = []
        word = lambda offset: struct.unpack_from(">I", data, offset)[0]
        for kind, count, offsets, addresses, sizes in [
            ("text", 7, 0, 0x48, 0x90), ("data", 11, 0x1C, 0x64, 0xAC)
        ]:
            for i in range(count):
                offset, address, size = (word(base + i * 4) for base in (offsets, addresses, sizes))
                if not size:
                    continue
                if offset < 0x100 or offset + size > len(data):
                    raise ValueError(f"{kind}{i}: section exceeds file bounds")
                self.check_mem1(address, size)
                self.sections.append(dict(kind=kind, index=i, offset=offset, address=address, size=size))
        for i, section in enumerate(self.sections):
            for other in self.sections[:i]:
                for start in ("offset", "address"):
                    if max(section[start], other[start]) < min(section[start] + section["size"], other[start] + other["size"]):
                        raise ValueError(f"Overlapping DOL sections ({start})")
        self.bss_address, self.bss_size, self.entry = word(0xD8), word(0xDC), word(0xE0)
        if self.bss_size:
            self.check_mem1(self.bss_address, self.bss_size)
        self.code(self.entry, 4)

    @staticmethod
    def check_mem1(address, size):
        if not MEM1_BASE <= address < address + size <= MEM1_END:
            raise ValueError(f"0x{address:08X}+0x{size:X}: outside the MEM1 translation window")

    def code(self, address, size):
        for section in self.sections:
            if section["kind"] == "text" and section["address"] <= address and address + size <= section["address"] + section["size"]:
                offset = section["offset"] + address - section["address"]
                return self.data[offset:offset + size]
        raise ValueError(f"0x{address:08X}+0x{size:X}: not in a DOL executable section")

    def first_call(self):
        for address in range(self.entry, self.entry + 0x40, 4):
            word = struct.unpack(">I", self.code(address, 4))[0]
            if word >> 26 == 18 and word & 1:
                displacement = word & 0x03FFFFFC
                if displacement & 0x02000000:
                    displacement -= 0x04000000
                return (displacement if word & 2 else address + displacement) & 0xFFFFFFFF
        raise ValueError("No direct startup call found in the first 0x40 bytes")

    def sda_bases(self, address, size, registers=(2, 13)):
        """Read adjacent lis/addi or lis/ori pairs; never guess an unknown sequence."""
        code = self.code(address, size)
        words = struct.unpack(f">{len(code) // 4}I", code)
        bases, evidence = {}, []
        for i in range(len(words) - 1):
            upper, lower = words[i:i + 2]
            register = (upper >> 21) & 31
            if upper >> 26 != 15 or (upper >> 16) & 31 or register not in registers:
                continue
            if (lower >> 21) & 31 != register or (lower >> 16) & 31 != register:
                continue
            low = lower & 0xFFFF
            if lower >> 26 == 14:
                low = low - 0x10000 if low & 0x8000 else low
                value = ((upper & 0xFFFF) << 16) + low
            elif lower >> 26 == 24:
                value = ((upper & 0xFFFF) << 16) | low
            else:
                continue
            if register in bases:
                raise ValueError(f"Multiple initialization pairs for r{register}")
            bases[register] = value & 0xFFFFFFFF
            evidence.append(dict(address=f"0x{address + i * 4:08X}", register=f"r{register}",
                                 words=[f"{upper:08X}", f"{lower:08X}"], value=f"0x{bases[register]:08X}"))
        if set(bases) != set(registers):
            raise ValueError("Could not establish " + " and ".join(f"r{r}" for r in registers) + " from initialization pairs")
        return bases, evidence


def parse_symbols(text, code_sections):
    entries = {}
    pattern = re.compile(r"^(.+?)\s*=\s*([^:]+):(0x[0-9A-Fa-f]+);\s*//.*\btype:function\b.*?\bsize:(0x[0-9A-Fa-f]+)")
    for number, line in enumerate(text.splitlines(), 1):
        if not re.search(r"\btype:function\b", line):
            continue
        match = pattern.match(line)
        if not match:
            raise ValueError(f"Unrecognized function symbol at line {number}")
        name, section, address, size = match.groups()
        if section not in code_sections:
            raise ValueError(f"Function {name} is not in a declared code section")
        address, size = int(address, 16), int(size, 16)
        if address % 4 or size <= 0 or size % 4:
            raise ValueError(f"Unaligned or empty function {name}")
        entries.setdefault(address, (address, name.strip(), size))
    if not entries:
        raise ValueError("No function symbols found")
    return sorted(entries.values())


def community_symbols():
    config = ROOT / "vendor/key/config/RK5E01"
    published = re.search(r"^hash:\s*(\w+)", (config / "config.yml").read_text(), re.M)
    if not published or published[1] != EXPECTED_SHA1:
        raise ValueError("Swiftshine/key revision hash does not match the pinned target")
    sections = set(re.findall(r"^\s+(\S+)\s+type:code\b", (config / "splits.txt").read_text(), re.M))
    entries = parse_symbols((config / "symbols.txt").read_text(), sections)
    return entries


def write_map(entries):
    path = ROOT / "local/RK5E01-functions.txt"
    path.parent.mkdir(exist_ok=True)
    # FunctionMap.Parse expects hexadecimal digits WITHOUT a 0x prefix.
    path.write_text("".join(f"{address:08X} {name}\n" for address, name, _ in entries))
    print(f"Function map: {len(entries)} unique functions -> {path}")


def looks_like_rel(path, header):
    """Recognize standalone, unlinked REL headers, including modules named .bin."""
    if len(header) < 0x40:
        return False
    fields = struct.unpack(">16I", header[:0x40])
    _, next_module, previous_module, count, table, _, _, version, _, reloc, imports, import_size = fields[:12]
    size = path.stat().st_size
    if next_module or previous_module or not (1 <= version <= 3 and 1 <= count <= 64):
        return False
    if table < 0x40 or table % 4 or table + count * 8 > size or imports + import_size > size or import_size % 8 or reloc > size:
        return False
    executable = False
    with path.open("rb") as stream:
        stream.seek(table)
        for offset, length in struct.iter_unpack(">II", stream.read(count * 8)):
            executable |= bool(offset & 1)
            offset &= ~3
            if offset and (offset < 0x40 or offset + length > size):
                return False
    return executable


def prepare(path):
    if not path.is_file():
        raise ValueError(f"Game DOL not found: {path}. Extract your WBFS or supply --dol /path/to/sys/main.dol")
    data = path.read_bytes()
    verify_hash(data, EXPECTED_SHA1)
    dol = Dol(data)
    entries = community_symbols()
    by_name = {name: (address, size) for address, name, size in entries}
    if dol.entry != by_name["__start"][0]:
        raise ValueError("DOL entry does not match the community __start symbol")
    init, size = by_name["__init_registers"]
    if dol.first_call() != init:
        raise ValueError("First startup call does not target __init_registers")
    bases, evidence = dol.sda_bases(init, size)
    stack, _ = dol.sda_bases(init, size, (1,))
    # Reserve an additional 8 KiB above the initial stack for the SDK debug
    # stack. Metadata placement must stay above this and all DOL/BSS ranges.
    protected_end = max(stack[1] + 0x2020, dol.bss_address + dol.bss_size,
                        *(s['address'] + s['size'] for s in dol.sections))
    dol.check_mem1(protected_end, 1)
    for address, _, size in entries:
        dol.code(address, size)
    for value in bases.values():
        dol.check_mem1(value, 1)
    # Confirm the SDK's exception-table global against both its symbol and
    # the actual lwz r?, displacement(r13) in __OSSetExceptionHandler.
    setter = by_name["__OSSetExceptionHandler"][0]
    load = struct.unpack(">I", dol.code(setter, 4))[0]
    displacement = load & 0xFFFF
    if displacement & 0x8000:
        displacement -= 0x10000
    exception_table_pointer = bases[13] + displacement
    source_symbols = (ROOT / "vendor/key/config/RK5E01/symbols.txt").read_text()
    pointer_symbol = re.search(r"^OSExceptionTable\s*=\s*\.sbss:(0x[0-9A-Fa-f]+);", source_symbols, re.M)
    if load >> 26 != 32 or (load >> 16) & 31 != 13 or not pointer_symbol or int(pointer_symbol[1], 16) != exception_table_pointer:
        raise ValueError("Could not confirm the SDK exception-table pointer against the DOL")
    dol.check_mem1(exception_table_pointer, 4)
    scb_symbol = re.search(r"^Scb\s*=\s*\.bss:(0x[0-9A-Fa-f]+);.*size:0x54\b", source_symbols, re.M)
    if not scb_symbol:
        raise ValueError("Missing SRAM control block symbol")
    scb = int(scb_symbol[1], 16)
    words = struct.unpack(">128I", dol.code(by_name["__OSInitSram"][0], 0x200))
    def loads_scb(upper, lower):
        reg = (upper >> 21) & 31
        low = lower & 0xFFFF
        if low & 0x8000:
            low -= 0x10000
        return (upper >> 26 == 15 and (upper >> 16) & 31 == 0 and lower >> 26 == 14
                and (lower >> 21) & 31 == reg and (lower >> 16) & 31 == reg
                and (((upper & 0xFFFF) << 16) + low) & 0xFFFFFFFF == scb)
    if not any(loads_scb(a, b) for a, b in zip(words, words[1:])):
        raise ValueError("SRAM initializer does not load the expected control block")
    dol.check_mem1(scb, 0x54)
    dsp_symbol = re.search(r"^DSPInitCode\s*=\s*\.data:(0x[0-9A-Fa-f]+);.*size:0x80\b", source_symbols, re.M)
    if not dsp_symbol:
        raise ValueError("Missing DSP initialization microcode symbol")
    dsp_code = int(dsp_symbol[1], 16)
    if not any(s['kind'] == 'data' and s['address'] <= dsp_code and
               dsp_code + 128 <= s['address'] + s['size'] for s in dol.sections):
        raise ValueError("DSP initialization microcode is not in a loaded data section")
    audio_address, audio_size = by_name['__OSInitAudioSystem']
    audio_words = struct.unpack(f'>{audio_size // 4}I', dol.code(audio_address, audio_size))
    # Verify memcpy's source and size: lis r4; lis r3 (work buffer);
    # addi r4,r4; li r5,128. The intervening instruction cannot change r4.
    def dsp_copy_arguments(i):
        a, b, c, d = audio_words[i:i+4]
        lo = c & 0xFFFF
        lo -= 0x10000 if lo & 0x8000 else 0
        return (a & 0xFFFF0000 == 0x3C800000 and b == 0x3C608100 and
                c & 0xFFFF0000 == 0x38840000 and d == 0x38A00080 and
                (((a & 0xFFFF) << 16) + lo) & 0xFFFFFFFF == dsp_code)
    if not any(dsp_copy_arguments(i) for i in range(len(audio_words) - 3)):
        raise ValueError("Audio initializer does not copy the verified DSP microcode to its work buffer")
    # The only native SDK override is the request transport. Check its symbol,
    # heap load and free call against this DOL, never another game's addresses.
    transport, transport_size = by_name['__ios_Ipc2']
    if (transport, transport_size) != (0x806A8FA0, 0x268):
        raise ValueError("IOS transport differs from the audited RK5E01 function")
    restore, restore_size = by_name['OSRestoreInterrupts']
    if ((restore, restore_size) != (0x806681C0, 0x24) or
        hashlib.sha256(dol.code(restore, restore_size)).hexdigest() !=
            '54db610a580003e2db23e460425f4662f4f4e7a4da3ae6f89a632df9e52b427a'):
        raise ValueError("OSRestoreInterrupts differs from the audited MSR/CR/register sequence")
    def object_address(name):
        match = re.search(rf"^{name}\s*=\s*\.[a-z0-9]+:(0x[0-9A-Fa-f]+);.*type:object size:0x4\b", source_symbols, re.M)
        if not match:
            raise ValueError(f"Missing 4-byte SDK global: {name}")
        value = int(match[1], 16)
        dol.check_mem1(value, 4)
        return value
    # These SDK names are still anonymous in the community map. Verify the
    # reviewed RK5E01 wrappers rather than importing another game's addresses.
    dvd_functions = {
        'DvdReadDiskId': ('fn_80676B00', 0x18C, '9b3f9a08f42853b31bf4a4b29ce69798a7d0ef85e68ef391dc6fa831b28d2166'),
        'DvdRead': ('fn_80678410', 0x1B0, 'd66b8804d900a7bd20304a36206c6b0693e36f264014d1d2ba2d5a8fde0fbf6f'),
        'DvdProbeCallback': ('fn_806750A0', 8, '9626a309acd3b739172c3012393d3eb49f3765b9d3896fa544857e31830b958b'),
    }
    dvd_addresses = {'DvdInit': by_name['DVDLowInit'][0]}
    for label, (symbol, expected_size, digest) in dvd_functions.items():
        address, size = by_name[symbol]
        if size != expected_size or hashlib.sha256(dol.code(address, size)).hexdigest() != digest:
            raise ValueError(f"DVD probe function differs from audited RK5E01 code: {symbol}")
        dvd_addresses[label] = address
    callback_store = struct.unpack('>I', dol.code(dvd_addresses['DvdProbeCallback'], 4))[0]
    if callback_store >> 16 != 0x906D:  # stw r3,disp(r13)
        raise ValueError('DVD completion callback no longer stores its result through r13')
    dvd_addresses['DvdProbeResult'] = bases[13] + struct.unpack('>h', struct.pack('>H', callback_store & 0xFFFF))[0]
    dol.check_mem1(dvd_addresses['DvdProbeResult'], 4)
    heap_id = object_address('hid')
    transport_words = struct.unpack(f'>{transport_size // 4}I', dol.code(transport, transport_size))
    heap_load = 0x806D0000 | ((heap_id - bases[13]) & 0xFFFF)  # lwz r3,disp(r13)
    free_address = by_name['iosFree'][0]
    if not any(word == heap_load and i + 2 < len(transport_words) and
               transport_words[i+2] == (0x48000001 | ((free_address - (transport + (i+2)*4)) & 0x03FFFFFC))
               for i, word in enumerate(transport_words)):
        raise ValueError("IOS transport no longer frees requests through the audited SDK heap")
    stm_globals = {name: object_address(name) for name in ('StmReady', 'StmImDesc', 'StmEhDesc', 'StmEhRegistered')}
    close_address, close_size = by_name['fn_806A9520']
    close_words = struct.unpack(f'>{close_size // 4}I', dol.code(close_address, close_size))
    # The unnamed SDK wrapper prepares command 2 then stores it at request+0.
    if 0x38000002 not in close_words or 0x90030000 not in close_words:
        raise ValueError("Unnamed IOS_Close wrapper does not prepare command 2")
    stm_address, stm_size = by_name['__OSInitSTM']
    stm_words = struct.unpack(f'>{stm_size // 4}I', dol.code(stm_address, stm_size))
    for name, address in stm_globals.items():
        if not any(w >> 26 in (32, 36) and (w >> 16) & 31 == 13 and
                   w & 0xFFFF == (address - bases[13]) & 0xFFFF for w in stm_words):
            raise ValueError(f"STM initializer does not reference {name}")
    scheduler = scheduler_audit.audit(dol, by_name, source_symbols, bases[13])
    wpad = wpad_audit.audit(dol, by_name, source_symbols)
    audio = audio_audit.audit(dol, by_name, source_symbols)
    cache = cache_audit.audit(dol, by_name)
    write_map(entries)
    generated = ROOT / "generated"
    generated.mkdir(exist_ok=True)
    (generated / "KirbyBootConfig.h").write_text(
        "// Generated from the verified RK5E01 DOL and community symbols.\n#pragma once\n#include <cstdint>\n"
        "namespace KirbyBootConfig {\n"
        f"inline constexpr uint32_t Entry = 0x{dol.entry:08X}u;\n"
        f"inline constexpr uint32_t InitRegisters = 0x{init:08X}u;\n"
        f"inline constexpr uint32_t ProtectedEnd = 0x{protected_end:08X}u;\n"
        f"inline constexpr uint32_t AudioClockInit = 0x{by_name['__AIClockInit'][0]:08X}u;\n"
        f"inline constexpr uint32_t AudioSystemInit = 0x{audio_address:08X}u;\n"
        f"inline constexpr uint32_t DspInitCode = 0x{dsp_code:08X}u;\n"
        f"inline constexpr uint32_t SetMem1ArenaHi = 0x{by_name['OSSetMEM1ArenaHi'][0]:08X}u;\n"
        f"inline constexpr uint32_t OsIpcBufferInit = 0x{by_name['__OSInitIPCBuffer'][0]:08X}u;\n"
        f"inline constexpr uint32_t IpcInit = 0x{by_name['IPCInit'][0]:08X}u;\n"
        f"inline constexpr uint32_t IpcClientInit = 0x{by_name['IPCCltInit'][0]:08X}u;\n"
        f"inline constexpr uint32_t IpcInterruptHandler = 0x{by_name['IPCInterruptHandler'][0]:08X}u;\n"
        f"inline constexpr uint32_t IpcGetBufferLo = 0x{by_name['IPCGetBufferLo'][0]:08X}u;\n"
        f"inline constexpr uint32_t IpcGetBufferHi = 0x{by_name['IPCGetBufferHi'][0]:08X}u;\n"
        f"inline constexpr uint32_t IosTransport = 0x{transport:08X}u;\n"
        f"inline constexpr uint32_t RestoreInterrupts = 0x{restore:08X}u;\n"
        f"inline constexpr uint32_t IosFree = 0x{free_address:08X}u;\n"
        f"inline constexpr uint32_t IosOpen = 0x{by_name['IOS_Open'][0]:08X}u;\n"
        f"inline constexpr uint32_t IosClose = 0x{close_address:08X}u;\n"
        f"inline constexpr uint32_t IpcHeapId = 0x{heap_id:08X}u;\n"
        f"inline constexpr uint32_t StmInit = 0x{stm_address:08X}u;\n"
        f"inline constexpr uint32_t StmUnregisterEvent = 0x{by_name['__OSUnRegisterStateEvent'][0]:08X}u;\n"
        f"inline constexpr uint32_t ScInit = 0x{by_name['SCInit'][0]:08X}u;\n"
        f"inline constexpr uint32_t EspInit = 0x{by_name['ESP_InitLib'][0]:08X}u;\n"
        f"inline constexpr uint32_t EspClose = 0x{by_name['ESP_CloseLib'][0]:08X}u;\n"
        f"inline constexpr uint32_t EspTicketView = 0x{by_name['ESP_DiGetTicketView'][0]:08X}u;\n"
        f"inline constexpr uint32_t EspTmd = 0x{by_name['ESP_DiGetTmd'][0]:08X}u;\n"
        f"inline constexpr uint32_t EspConsumption = 0x{by_name['fn_806FF740'][0]:08X}u;\n"
        f"inline constexpr uint32_t AveI2cWrite = 0x{by_name['fn_8067BBD0'][0]:08X}u;\n"
        f"inline constexpr uint32_t ScCheckStatus = 0x{by_name['SCCheckStatus'][0]:08X}u;\n"
        f"inline constexpr uint32_t ScGetLanguage = 0x{by_name['SCGetLanguage'][0]:08X}u;\n"
        f"inline constexpr uint32_t ScItemCount = 0x{object_address('ItemNumTotal'):08X}u;\n"
        + ''.join(f"inline constexpr uint32_t {name} = 0x{address:08X}u;\n" for name, address in scheduler.items())
        + ''.join(f"inline constexpr uint32_t {name} = 0x{address:08X}u;\n" for name, address in wpad.items())
        + ''.join(f"inline constexpr uint32_t {name} = 0x{address:08X}u;\n" for name, address in audio.items())
        + ''.join(f"inline constexpr uint32_t {name} = 0x{address:08X}u;\n" for name, address in cache.items())
        + ''.join(f"inline constexpr uint32_t {name} = 0x{address:08X}u;\n" for name, address in dvd_addresses.items())
        + ''.join(f"inline constexpr uint32_t {name} = 0x{address:08X}u;\n" for name, address in stm_globals.items()) +
        f"inline constexpr uint32_t InterruptInit = 0x{by_name['__OSInterruptInit'][0]:08X}u;\n"
        f"inline constexpr uint32_t ExiInit = 0x{by_name['EXIInit'][0]:08X}u;\n"
        f"inline constexpr uint32_t SiInit = 0x{by_name['SIInit'][0]:08X}u;\n"
        f"inline constexpr uint32_t SramInit = 0x{by_name['__OSInitSram'][0]:08X}u;\n"
        f"inline constexpr uint32_t SramControl = 0x{scb:08X}u;\n"
        f"inline constexpr uint32_t ExternalInterruptHandler = 0x{by_name['ExternalInterruptHandler'][0]:08X}u;\n"
        f"inline constexpr uint32_t ExceptionTablePointer = 0x{exception_table_pointer:08X}u;\n"
        f"inline constexpr uint32_t MemoryBase = 0x{MEM1_BASE:08X}u;\n"
        f"inline constexpr uint32_t MemorySize = 0x{MEM1_END - MEM1_BASE:08X}u;\n"
        "// Retail single-layer geometry; RK5E01 WBFS allocation audit in docs/bringup.md.\n"
        "inline constexpr uint64_t DiscCapacity = 0x118240000ull;\n"
        "}\n")
    sha256 = hashlib.sha256(data).hexdigest()
    manifest = (ROOT / "projects/RK5E01/recomp.yml").read_text()
    for expected in (f"sda_base: 0x{bases[13]:08X}", f"sda2_base: 0x{bases[2]:08X}",
                     f"sha256: {sha256}", f"- 0x{dol.entry:08X}"):
        if expected not in manifest:
            raise ValueError(f"Checked-in profile disagrees with the DOL: expected {expected}")
    manifest = manifest.replace("workspace_root: ../..", f"workspace_root: {json.dumps(ROOT.as_posix())}")
    manifest = manifest.replace("path: disc/DATA/sys/main.dol", f"path: {json.dumps(path.resolve().as_posix())}")
    output = ROOT / "local/RK5E01.yml"
    output.write_text(manifest)
    files = path.parent.parent / "files" if path.parent.name == "sys" else None
    modules = []
    if files and files.is_dir():
        for item in files.rglob("*"):
            if item.is_file():
                with item.open("rb") as stream:
                    header = stream.read(0x4C)
                candidate = looks_like_rel(item, header)
                if item.suffix.lower() == ".rel" or candidate:
                    modules.append(str(item.relative_to(files)))
    report = dict(sha1=EXPECTED_SHA1, sha256=sha256, entry=f"0x{dol.entry:08X}",
                  os_exception_table_pointer=f"0x{exception_table_pointer:08X}",
                  sections=dol.sections, bss_address=dol.bss_address, bss_size=dol.bss_size,
                  sda_evidence=evidence, initial_stack=f"0x{stack[1]:08X}",
                  boot_metadata_lower_bound=f"0x{protected_end:08X}", function_count=len(entries),
                  files_root=str(files) if files and files.is_dir() else None,
                  possible_rel_modules=modules,
                  module_scan="Header and extension candidates only; compressed/embedded modules require further inspection")
    (ROOT / "local/dol-report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    print(f"Profile: {output}")
    return dol.entry, output


def run_logged(name, command):
    logs = ROOT / "build-logs"
    logs.mkdir(exist_ok=True)
    print("+ " + " ".join(map(str, command)), flush=True)
    with (logs / f"{name}.log").open("w") as log:
        result = subprocess.run(list(map(str, command)), cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
    if result.returncode:
        print((logs / f"{name}.log").read_text()[-8000:], file=sys.stderr)
        raise ValueError(f"{name} failed (exit {result.returncode}); see build-logs/{name}.log")
    print(f"{name}: passed; build-logs/{name}.log")


def translator(existing=None):
    if existing is not None:
        if not existing.is_file():
            raise ValueError(f"Translator DLL not found: {existing}")
        return ["dotnet", existing.resolve()]
    project = ROOT / "vendor/wiicompiled/translator/src/Translator.Cli"
    run_logged("translator-build", ["dotnet", "build", project / "Translator.Cli.csproj", "-c", "Release"])
    return ["dotnet", project / "bin/Release/net8.0/Translator.Cli.dll"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=["doctor", "map", "prepare", "translator", "translate", "shards"])
    parser.add_argument("--dol", type=Path, default=ROOT / "disc/DATA/sys/main.dol")
    parser.add_argument("--threads", type=int, default=2)
    parser.add_argument("--translator-dll", type=Path, help="Use an explicitly prebuilt translator without NuGet restore")
    args = parser.parse_args()
    try:
        if args.command == "doctor":
            missing = []
            for tool in ("git", "python3", "dotnet", "clang", "clang++", "cmake", "ninja", "pkg-config"):
                found = shutil.which(tool)
                print(f"{tool}: {found or 'MISSING'}")
                if not found:
                    missing.append(tool)
            if missing:
                raise ValueError("Missing tools: " + ", ".join(missing) + ". Use tools/container.sh")
        elif args.command == "map":
            write_map(community_symbols())
        elif args.command == "translator":
            translator(args.translator_dll)
        else:
            entry, profile = prepare(args.dol)
            if args.command in ("translate", "shards"):
                tr = translator(args.translator_dll)
            if args.command == "translate":
                run_logged("info", [*tr, "info", "--project", profile])
                run_logged("translate", [*tr, "translate-recursive", f"0x{entry:08X}", "--project", profile,
                                         "--outdir", ROOT / "generated/functions", "--output-metadata",
                                         ROOT / "generated/base_translation_output.json", "--prune-stale",
                                         "--threads", str(max(1, args.threads))])
                run_logged("data-init", [*tr, "generate-data-init", "--project", profile])
            if args.command in ("translate", "shards"):
                run_logged("shards", [*tr, "emit-build-shards", "--project", profile,
                                      "--base-metadata", ROOT / "generated/base_translation_output.json",
                                      "--base-functions-dir", ROOT / "generated/functions",
                                      "--native-source-dir", ROOT / "runtime/native-src",
                                      "--native-index", ROOT / "local/native-index.json",
                                      "--out", ROOT / "generated/build_shards"])
                print("Build shards emitted. Rebuild the native executable before testing.")
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
