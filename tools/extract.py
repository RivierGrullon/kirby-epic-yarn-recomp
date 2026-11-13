#!/usr/bin/env python3
"""Extract a local WBFS (optionally inside 7z) using Dolphin's DATA-partition export."""
import argparse
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys

from kirby import EXPECTED_SHA1, ROOT, verify_hash


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    args = parser.parse_args()
    source = args.source.resolve()
    try:
        if not source.is_file():
            raise ValueError(f"Input not found: {source}")
        dol = ROOT / "disc/DATA/sys/main.dol"
        if dol.is_file():
            verify_hash(dol.read_bytes(), EXPECTED_SHA1)
            if (ROOT / "disc/.extraction-complete").is_file():
                print("Verified extraction already exists in disc/DATA; left untouched")
                return 0
        if source.suffix.lower() == ".7z":
            sevenzip = shutil.which("7z") or shutil.which("7zz")
            if not sevenzip:
                raise ValueError("7z or 7zz is required to unpack this archive")
            listing = subprocess.check_output([sevenzip, "l", "-slt", str(source)], text=True)
            members = [line[7:] for line in listing.splitlines() if line.startswith("Path = ") and line.lower().endswith(".wbfs")]
            if len(members) != 1:
                raise ValueError("Archive must contain exactly one WBFS file")
            member = PurePosixPath(members[0])
            if member.is_absolute() or ".." in member.parts or "\\" in members[0]:
                raise ValueError("Unsafe archive member path")
            destination = ROOT / "local/images"
            destination.mkdir(parents=True, exist_ok=True)
            # Existing members are tested against the archive before reuse.
            target = destination / members[0]
            if target.exists():
                import zlib
                metadata = next(block for block in listing.split("\n\n") if f"Path = {members[0]}\n" in block)
                expected_crc = next(line[6:] for line in metadata.splitlines() if line.startswith("CRC = "))
                crc = 0
                with target.open("rb") as stream:
                    while chunk := stream.read(1024 * 1024):
                        crc = zlib.crc32(chunk, crc)
                if f"{crc:08X}" != expected_crc.upper():
                    raise ValueError(f"Existing WBFS failed archive CRC: {target}; left untouched")
            else:
                subprocess.run([sevenzip, "x", "-y", f"-o{destination}", str(source), members[0]], check=True)
            source = target
        if source.suffix.lower() not in (".wbfs", ".iso", ".rvz"):
            raise ValueError("Supply .wbfs, .iso, .rvz, or a .7z containing one WBFS")
        output = ROOT / "disc"
        if output.exists() and any(output.iterdir()):
            raise ValueError("disc/ is nonempty without a completion marker; move the partial extraction before retrying")
        output.mkdir(exist_ok=True)
        if shutil.which("dolphin-tool"):
            command = ["dolphin-tool"]
        else:
            command = ["flatpak", "run", f"--filesystem={source}:ro", f"--filesystem={output}",
                       "--command=dolphin-tool", "org.DolphinEmu.dolphin-emu"]
        subprocess.run([*command, "extract", "-i", str(source), "-o", str(output), "-g", "-q"], check=True)
        verify_hash(dol.read_bytes(), EXPECTED_SHA1)
        if not (dol.parent.parent / "files").is_dir():
            raise ValueError("Dolphin extraction has no files/ directory")
        (output / ".extraction-complete").write_text(EXPECTED_SHA1 + "\n")
        print(f"Verified RK5E01 Rev 0: {dol}")
        return 0
    except (OSError, ValueError, StopIteration, subprocess.CalledProcessError) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
