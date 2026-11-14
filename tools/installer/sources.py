# SPDX-License-Identifier: GPL-3.0-only
"""Inspect the selected source and stage Dolphin exports by source SHA-256."""
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
import tempfile

from .data import DISC_CAPACITY, inspect_data
from .jobs import run_process


def _stamp(path):
    info = path.stat()
    return info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns


def _hash_source(path, job):
    digest = hashlib.sha256()
    completed = 0
    before = _stamp(path)
    if not stat.S_ISREG(path.stat().st_mode):
        raise ValueError("Selected image must be a regular file")
    job.emit(dict(stage="hash", state="started", completed=0, total=before[2]))
    with path.open("rb") as stream:
        while chunk := stream.read(4 * 1024 * 1024):
            job.check_cancel()
            digest.update(chunk)
            completed += len(chunk)
            if completed % (64 * 1024 * 1024) == 0:
                job.emit(dict(stage="hash", state="progress", completed=completed, total=before[2]))
    if before != _stamp(path) or completed != before[2]:
        raise ValueError("Selected source changed while hashing")
    job.emit(dict(stage="hash", state="complete", completed=completed, total=completed))
    return digest.hexdigest(), before


def archive_member(listing):
    """Parse bounded 7z -slt -ba output and select one literal, safe WBFS."""
    members = []
    for block in listing.replace("\r\n", "\n").split("\n\n"):
        fields = dict(line.split(" = ", 1) for line in block.splitlines() if " = " in line)
        name = fields.get("Path", "")
        if not name.lower().endswith(".wbfs"):
            continue
        path = PurePosixPath(name)
        if (not name or path.is_absolute() or ".." in path.parts or "\\" in name or "\0" in name
                or fields.get("Folder") == "+" or fields.get("Encrypted") == "+"
                or any("link" in key.lower() for key in fields)
                or any(token.startswith("l") for token in fields.get("Attributes", "").split())):
            raise ValueError("Unsafe, encrypted or linked archive WBFS member")
        size = int(fields.get("Size", "0"))
        if not 0 < size <= DISC_CAPACITY + 1024 * 1024 * 1024:
            raise ValueError("Invalid archive WBFS size")
        members.append((name, size))
    if len(members) != 1:
        raise ValueError("Archive must contain exactly one WBFS file")
    return members[0]


def _dolphin(source, output):
    native = shutil.which("dolphin-tool")
    if native:
        return [native]
    flatpak = shutil.which("flatpak")
    if not flatpak:
        raise ValueError("Dolphin's dolphin-tool or the Dolphin Flatpak is required")
    return [flatpak, "run", f"--filesystem={source}:ro", f"--filesystem={output}",
            "--command=dolphin-tool", "org.DolphinEmu.dolphin-emu"]


def inspect_source(selected, job):
    """Return a verified DATA source; never replace a runnable build or user data."""
    job.check_cancel()
    source = Path(selected).absolute()
    if source.is_dir():
        job.emit(dict(stage="validate", state="started"))
        result = inspect_data(source, check_cancel=job.check_cancel)
        return dict(result, source=str(source), source_kind="directory")
    if not source.is_file():
        raise ValueError(f"Selected source does not exist: {source}")
    if source.suffix.lower() not in (".7z", ".wbfs", ".iso", ".rvz"):
        raise ValueError("Select a DATA folder, WBFS, ISO, RVZ, or 7z containing one WBFS")
    source = source.resolve(strict=True)
    digest, stamp = _hash_source(source, job)
    cache = job.workspace / "sources" / digest
    manifest = dict(version=1, source_sha256=digest, source_bytes=stamp[2])
    if cache.exists():
        if cache.is_symlink() or (cache / "source.json").is_symlink() or not (cache / "source.json").is_file():
            raise ValueError("Setup cache requires regular, unlinked metadata")
        with (cache / "source.json").open("rb") as stream:
            data = stream.read(4097)
        if len(data) > 4096 or json.loads(data) != manifest:
            raise ValueError("Setup cache manifest does not match selected source")
        job.emit(dict(stage="validate", state="started", cached=True))
        result = inspect_data(cache / "export", check_cancel=job.check_cancel)
        return dict(result, source=str(source), source_kind=source.suffix[1:].lower(), source_sha256=digest, cached=True)
    with tempfile.TemporaryDirectory(prefix="inspect-", dir=job.workspace / "staging") as directory:
        staging = Path(directory)
        image = source
        needed = DISC_CAPACITY  # Conservative DATA export allowance, not a progress total.
        if source.suffix.lower() == ".7z":
            sevenzip = shutil.which("7z") or shutil.which("7zz")
            if not sevenzip:
                raise ValueError("7z or 7zz is required for archive imports")
            listing = run_process(job, [sevenzip, "l", "-slt", "-ba", "--", source], capture_limit=2 * 1024 * 1024)
            name, size = archive_member(listing.decode("utf-8", errors="strict"))
            needed += size
            if shutil.disk_usage(staging).free < needed:
                raise ValueError(f"Not enough free space for staged import: need at least {needed} bytes")
            images = staging / "image"
            images.mkdir(mode=0o700)
            job.emit(dict(stage="unpack", state="started"))
            # -spd disables wildcard expansion; -- ends switch parsing for filenames.
            run_process(job, [sevenzip, "x", "-y", "-spd", f"-o{images}", "--", source, name])
            image = images / name
            if (not image.resolve().is_relative_to(images.resolve())
                    or any((images / Path(*PurePosixPath(name).parts[:i])).is_symlink()
                           for i in range(1, len(PurePosixPath(name).parts) + 1))
                    or not image.is_file() or image.stat().st_size != size):
                raise ValueError("Archive did not produce the selected regular WBFS")
            job.emit(dict(stage="unpack", state="complete"))
        elif shutil.disk_usage(staging).free < needed:
            raise ValueError(f"Not enough free space for staged DATA export: need at least {needed} bytes")
        output = staging / "export"
        output.mkdir(mode=0o700)
        job.emit(dict(stage="extract", state="started"))
        run_process(job, [*_dolphin(image, output), "extract", "-i", image, "-o", output, "-g", "-q"])
        job.emit(dict(stage="extract", state="complete"))
        job.emit(dict(stage="validate", state="started", cached=False))
        result = inspect_data(output, check_cancel=job.check_cancel)
        if stamp != _stamp(source):
            raise ValueError("Selected source changed during extraction")
        if (staging / "image").exists():
            shutil.rmtree(staging / "image")
        (staging / "source.json").write_text(json.dumps(manifest) + "\n")
        job.check_cancel()
        cache.parent.mkdir(exist_ok=True, mode=0o700)
        if cache.parent.is_symlink():
            raise ValueError("Setup cache must not be a symlink")
        relative_data = Path(result["data_root"]).relative_to(staging.resolve())
        os.rename(staging, cache)
        result["data_root"] = str(cache / relative_data)
        return dict(result, source=str(source), source_kind=source.suffix[1:].lower(), source_sha256=digest, cached=False)
