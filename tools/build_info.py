# SPDX-License-Identifier: GPL-3.0-only
"""Generate build provenance for the authored checkout (not a binary checksum)."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import subprocess
import tempfile
from kirby import EXPECTED_SHA1


def collect(root, *, compiler, build_type, compiler_options=""):
    root = Path(root).resolve()

    def git(*arguments):
        try:
            return subprocess.check_output(["git", "-C", str(root), *arguments], stderr=subprocess.PIPE)
        except (OSError, subprocess.CalledProcessError) as error:
            raise ValueError("Build information requires a Git checkout root with a commit") from error

    if Path(os.fsdecode(git("rev-parse", "--show-toplevel")).strip()).resolve() != root:
        raise ValueError("Build information must use the checkout root")
    revision = git("rev-parse", "HEAD").decode().strip()
    if not re.fullmatch(r"[0-9a-f]{40}", revision):
        raise ValueError("Unsupported project revision identifier")
    digest = hashlib.sha256()
    paths = sorted(set(git("ls-files", "-z", "--cached", "--others", "--exclude-standard").split(b"\0")) - {b""})
    for name in paths:
        path = root / os.fsdecode(name)
        digest.update(len(name).to_bytes(8, "big") + name)
        try:
            mode = path.lstat().st_mode
        except FileNotFoundError:
            digest.update(b"missing\0")
            continue
        if stat.S_ISLNK(mode):
            body = os.fsencode(os.readlink(path))
            kind = b"link"
        elif stat.S_ISREG(mode):
            body = path.read_bytes()
            kind = b"file" + bytes([mode & 0o111])
        else:
            raise ValueError(f"Cannot fingerprint nonregular project input: {os.fsdecode(name)}")
        digest.update(kind + b"\0" + len(body).to_bytes(8, "big") + hashlib.sha256(body).digest())
    dependencies = json.loads((root / "dependencies.json").read_text())
    translator = dependencies["wiicompiled"]["commit"]
    aurora = dependencies["wiicompiled"]["submodules"]["aurora-main"]
    if any(not isinstance(pin, str) or not re.fullmatch(r"[0-9a-f]{40}", pin) for pin in (translator, aurora)):
        raise ValueError("Build dependency pins must be full Git revisions")
    return {"source_revision": revision, "source_modified": bool(git("status", "--porcelain", "-z", "--untracked-files=all")),
            "source_sha256": digest.hexdigest(), "translator_revision": translator, "aurora_revision": aurora,
            "compiler": compiler, "build_type": build_type, "compiler_options": compiler_options,
            "game_id": "RK5E01", "game_revision": 0, "dol_sha1": EXPECTED_SHA1}


def write_header(output, record):
    output = Path(output)
    if output.is_symlink():
        raise ValueError("Build information output cannot be a symlink")
    document = "[build]\n"
    for key, value in record.items():
        document += key + " = " + json.dumps(value, ensure_ascii=False) + "\n"
    # Reserve space for the CLI schema/program envelope accepted by the parser.
    if len(document.encode())>16384-128:
        raise ValueError("Build information exceeds 16 KiB")
    number = 0
    while f')KBI{number}"' in document:
        number += 1
    delimiter = f"KBI{number}"
    body = ("// Generated build information; do not edit.\n#pragma once\n"
            "namespace Kirby::Generated {\ninline constexpr char BuildInfoDocument[] = R\"" +
            delimiter + "(" + document + ")" + delimiter + "\";\n"
            "inline constexpr char SupportedDolSha1[] = " + json.dumps(record["dol_sha1"]) + ";\n}\n").encode()
    if output.exists() and output.read_bytes() == body:
        return
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent, prefix=output.name + ".", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(body)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, output)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--compiler", required=True)
    parser.add_argument("--build-type", required=True)
    parser.add_argument("--compiler-options", default="")
    args = parser.parse_args()
    try:
        source = args.source.resolve()
        output = args.output.absolute()
        if output.is_relative_to(source) and subprocess.run(
                ["git", "-C", str(source), "check-ignore", "--quiet", "--", str(output.relative_to(source))],
                capture_output=True).returncode:
            raise ValueError("Build information output must be ignored or outside the checkout")
        write_header(output, collect(source, compiler=args.compiler, build_type=args.build_type,
                                     compiler_options=args.compiler_options))
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"Build information: {error}\n")


if __name__ == "__main__":
    main()
