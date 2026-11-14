# SPDX-License-Identifier: GPL-3.0-only
"""Validate a selected Dolphin DATA export against the native boot layout.

Checks the exact DOL, title metadata, FST paths, sizes and logical extents.
Asset contents and Nintendo signatures are not authenticated by this check.
"""
import os
from pathlib import Path
import stat
import struct

import kirby

# Same verified RK5E01 single-layer logical capacity as tools/kirby.py's profile.
DISC_CAPACITY = 0x118240000
TITLE_ID = 0x00010000524B3545


def _be(data, offset):
    return struct.unpack_from(">I", data, offset)[0]


class _DataFiles:
    def __init__(self, root):
        self.fd = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW | os.O_CLOEXEC)

    def close(self):
        os.close(self.fd)

    def open(self, path, *, directory=False):
        fd = os.dup(self.fd)
        try:
            parts = Path(path).parts
            if not parts or Path(path).is_absolute() or any(p in (".", "..") for p in parts):
                raise ValueError("Invalid DATA path")
            for index, part in enumerate(parts):
                flags = os.O_RDONLY | os.O_NOFOLLOW | os.O_CLOEXEC | os.O_NONBLOCK
                if index + 1 < len(parts) or directory:
                    flags |= os.O_DIRECTORY
                child = os.open(part, flags, dir_fd=fd)
                os.close(fd)
                fd = child
            if not directory and not stat.S_ISREG(os.fstat(fd).st_mode):
                raise ValueError(f"Not a regular DATA file: {path}")
            result, fd = fd, -1
            return result
        finally:
            if fd >= 0:
                os.close(fd)

    def size(self, path, *, directory=False):
        fd = self.open(path, directory=directory)
        try:
            return os.fstat(fd).st_size
        finally:
            os.close(fd)

    def read(self, path, limit):
        fd = self.open(path)
        try:
            before = os.fstat(fd)
            if before.st_size > limit:
                raise ValueError(f"Oversized DATA metadata: {path}")
            with os.fdopen(os.dup(fd), "rb") as stream:
                data = stream.read(limit + 1)
            after = os.fstat(fd)
            if len(data) != before.st_size or (before.st_size, before.st_mtime_ns) != (after.st_size, after.st_mtime_ns):
                raise ValueError(f"DATA file changed while reading: {path}")
            return data
        finally:
            os.close(fd)


def inspect_data(selected, *, check_cancel=lambda: None):
    """Inspect only the supplied DATA directory or its Dolphin export parent."""
    check_cancel()
    root = Path(selected).absolute()
    if not (root / "sys").is_dir() and (root / "DATA").is_dir():
        root /= "DATA"
    files = _DataFiles(root)
    try:
        boot = files.read("sys/boot.bin", 0x440)
        if (len(boot) != 0x440 or boot[:8] != b"RK5E01\0\0" or _be(boot, 0x18) != 0x5D1C9EA3):
            raise ValueError("RK5E01 Rev 0 Wii boot header required")
        dol_bytes = files.read("sys/main.dol", 24 * 1024 * 1024)
        kirby.verify_hash(dol_bytes, kirby.EXPECTED_SHA1)
        dol = kirby.Dol(dol_bytes)
        fst = files.read("sys/fst.bin", 32 * 1024 * 1024)
        if len(fst) < 12 or _be(boot, 0x428) * 4 != len(fst):
            raise ValueError("Invalid FST size")
        nodes = _be(fst, 8)
        if not nodes or nodes * 12 > len(fst) or _be(fst, 0) != 0x01000000 or _be(fst, 4):
            raise ValueError("Invalid FST root")
        files.size("files", directory=True)
        extents = []

        def add(start, path, expected):
            size = files.size(path)
            if size != expected:
                raise ValueError(f"DATA size mismatch: {path}")
            if start + size > DISC_CAPACITY:
                raise ValueError("DATA extent exceeds RK5E01 disc capacity")
            if size:
                extents.append((start, start + size))

        add(0, "sys/boot.bin", 0x440)
        add(0x440, "sys/bi2.bin", 0x2000)
        apploader = files.read("sys/apploader.img", 32 * 1024 * 1024)
        if len(apploader) < 32 or len(apploader) != 32 + _be(apploader, 0x14) + _be(apploader, 0x18):
            raise ValueError("Invalid apploader size")
        add(0x2440, "sys/apploader.img", len(apploader))
        add(_be(boot, 0x420) * 4, "sys/main.dol", len(dol_bytes))
        add(_be(boot, 0x424) * 4, "sys/fst.bin", len(fst))
        stack = [(0, nodes, Path("files"))]
        seen = set()
        count = total = 0
        for index in range(1, nodes):
            check_cancel()
            while index == stack[-1][1]:
                stack.pop()
            word, offset, size = struct.unpack_from(">III", fst, index * 12)
            kind, name_offset = word >> 24, nodes * 12 + (word & 0xFFFFFF)
            if kind > 1 or name_offset >= len(fst):
                raise ValueError("Invalid FST entry")
            end = fst.find(b"\0", name_offset)
            if end < 0:
                raise ValueError("Unterminated FST name")
            name = os.fsdecode(fst[name_offset:end])
            if not name or name in (".", "..") or "/" in name:
                raise ValueError("Invalid FST name")
            path = stack[-1][2] / name
            if path in seen:
                raise ValueError("Duplicate FST path")
            seen.add(path)
            if kind:
                if offset != stack[-1][0] or not index < size <= stack[-1][1]:
                    raise ValueError("Invalid FST directory bounds")
                files.size(path, directory=True)
                stack.append((index, size, path))
            else:
                add(offset * 4, path, size)
                count += 1
                total += size
        extents.sort()
        for previous, current in zip(extents, extents[1:]):
            if previous[1] > current[0]:
                raise ValueError("Overlapping DATA extents")
        ticket = files.read("ticket.bin", 0x2A4)
        if (len(ticket) != 0x2A4 or _be(ticket, 0) != 0x10001 or ticket[0x1BC] != 0
                or struct.unpack_from(">Q", ticket, 0x1DC)[0] != TITLE_ID):
            raise ValueError("Invalid RK5E01 ticket format or title ID")
        tmd = files.read("tmd.bin", 0x49E4)
        if (len(tmd) < 0x1E4 or _be(tmd, 0) != 0x10001 or tmd[0x180] != 0
                or struct.unpack_from(">Q", tmd, 0x18C)[0] != TITLE_ID
                or len(tmd) < 0x1E4 + struct.unpack_from(">H", tmd, 0x1DE)[0] * 36):
            raise ValueError("Invalid RK5E01 TMD format or content table")
        check_cancel()
        return dict(data_root=str(root.resolve()), game_id="RK5E01", revision=0,
                    dol_sha1=kirby.EXPECTED_SHA1, entry_point=dol.entry,
                    asset_files=count, asset_bytes=total,
                    validation="dol-sha1-and-data-layout")
    finally:
        files.close()
