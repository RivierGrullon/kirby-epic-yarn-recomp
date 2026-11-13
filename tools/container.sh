#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image=localhost/kirby-native-toolchain:stage1
if [[ "${1:-}" == --build ]]; then
    exec podman build -t "$image" -f "$root/tools/Containerfile" "$root/tools"
fi
if ! podman image exists "$image"; then
    echo 'Toolchain image missing. Run: bash tools/container.sh --build' >&2
    exit 1
fi
exec podman run --rm --userns=keep-id --security-opt label=disable \
    -e HOME=/tmp/kirby-build-home -e DOTNET_CLI_TELEMETRY_OPTOUT=1 -e DOTNET_NOLOGO=1 \
    -v "$root:/workspace:rw" -w /workspace "$image" "$@"
