#!/usr/bin/env python3
"""Fetch pinned source dependencies without overwriting existing checkouts."""
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    for name, pin in json.loads((ROOT / "dependencies.json").read_text()).items():
        path = ROOT / "vendor" / name
        if not path.exists():
            subprocess.run(["git", "init", str(path)], check=True)
            subprocess.run(["git", "-C", str(path), "remote", "add", "origin", pin["url"]], check=True)
            subprocess.run(["git", "-C", str(path), "fetch", "--depth=1", "origin", pin["commit"]], check=True)
            subprocess.run(["git", "-C", str(path), "checkout", "--detach", "FETCH_HEAD"], check=True)
        actual = subprocess.check_output(["git", "-C", str(path), "rev-parse", "HEAD"], text=True).strip()
        if actual != pin["commit"]:
            raise SystemExit(f"{path}: expected {pin['commit']}, found {actual}; checkout left untouched")
        if subprocess.check_output(["git", "-C", str(path), "status", "--porcelain"], text=True).strip():
            raise SystemExit(f"{path}: checkout has local changes; left untouched")
        print(f"{name}: {actual}")
        for submodule, commit in pin.get("submodules", {}).items():
            subprocess.run(["git", "-C", str(path), "submodule", "update", "--init", "--depth=1", submodule], check=True)
            actual = subprocess.check_output(["git", "-C", str(path / submodule), "rev-parse", "HEAD"], text=True).strip()
            if actual != commit:
                raise SystemExit(f"{submodule}: expected {commit}, found {actual}")
            print(f"{name}/{submodule}: {actual}")


if __name__ == "__main__":
    main()
