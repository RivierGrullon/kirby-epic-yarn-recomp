#!/usr/bin/env python3
"""Fetch pinned source dependencies without overwriting existing checkouts."""
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def use_lf(path):
    # Configure only repositories we just created. Windows Git otherwise checks
    # these sources out as CRLF, which cannot accept our Linux container patches.
    for key, value in (("core.autocrlf", "false"), ("core.eol", "lf")):
        subprocess.run(["git", "-C", str(path), "config", "--local", key, value], check=True)


def validate_checkout(path, expected):
    if not (path / ".git").exists():
        raise SystemExit(f"{path}: expected a Git checkout; existing directory left untouched")
    actual = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
    if actual != expected:
        raise SystemExit(f"{path}: expected {expected}, found {actual}; checkout left untouched")
    # Respect user content even if their Git settings hide dirty submodules.
    if subprocess.check_output(["git", "-C", str(path), "status", "--porcelain",
                                "--ignore-submodules=none"], text=True).strip():
        raise SystemExit(f"{path}: checkout has local changes; left untouched")
    return actual


def main():
    for name, pin in json.loads((ROOT / "dependencies.json").read_text()).items():
        path = ROOT / "vendor" / name
        if not path.exists():
            subprocess.run(["git", "init", str(path)], check=True)
            use_lf(path)
            subprocess.run(["git", "-C", str(path), "remote", "add", "origin", pin["url"]], check=True)
            subprocess.run(["git", "-C", str(path), "fetch", "--depth=1", "origin", pin["commit"]], check=True)
            subprocess.run(["git", "-C", str(path), "checkout", "--detach", "FETCH_HEAD"], check=True)
        actual = validate_checkout(path, pin["commit"])
        print(f"{name}: {actual}")
        for submodule, commit in pin.get("submodules", {}).items():
            child = path / submodule
            if not (child / ".git").exists():
                if child.exists() and (not child.is_dir() or any(child.iterdir())):
                    raise SystemExit(f"{child}: existing content left untouched")
                # -c reaches the new submodule's first checkout, before it has a
                # repository-local configuration of its own.
                subprocess.run(["git", "-c", "core.autocrlf=false", "-c", "core.eol=lf",
                                "-C", str(path), "submodule", "update", "--init", "--depth=1", submodule], check=True)
                use_lf(child)
            actual = validate_checkout(child, commit)
            print(f"{name}/{submodule}: {actual}")


if __name__ == "__main__":
    main()
