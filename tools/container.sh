#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
if ! command -v python3 >/dev/null 2>&1; then
    echo 'Python 3.11+ is required. See docs/building.md for setup requirements.' >&2
    exit 1
fi
exec python3 "$root/tools/container_engine.py" "$@"
