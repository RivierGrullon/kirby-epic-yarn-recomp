#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image=localhost/kirby-native-toolchain:stage1
if [[ "${1:-}" == --build ]]; then
    exec podman build -t "$image" -f "$root/tools/Containerfile" "$root/tools"
fi
managed_args=()
if [[ "${1:-}" == --managed ]]; then
    if [[ $# -lt 7 || "$6" != -- ]]; then
        echo 'Usage: container.sh --managed CIDFILE JOB TOKEN IMAGE_ID -- COMMAND [ARG...]' >&2
        exit 2
    fi
    cidfile="$2"; job="$3"; token="$4"; image="$5"
    if [[ -z "$cidfile" || ! "$job" =~ ^[A-Za-z0-9_-]{1,80}$ ||
          ! "$token" =~ ^[a-f0-9]{32}$ || ! "$image" =~ ^(sha256:)?[a-f0-9]{64}$ ]]; then
        echo 'Invalid managed container identity' >&2
        exit 2
    fi
    managed_args=(--cidfile "$cidfile" --name "kirby-setup-$token"
        --label "io.kirby.setup-job=$job" --label "io.kirby.setup-token=$token")
    shift 6
fi
if ! podman image exists "$image"; then
    echo 'Toolchain image missing. Run: bash tools/container.sh --build' >&2
    exit 1
fi
# Use the host resolver: rootless networking cannot always reach Bazzite's DNS stub.
exec podman run --rm --network=host --userns=keep-id --security-opt label=disable \
    "${managed_args[@]}" \
    -e HOME=/tmp/kirby-build-home -e DOTNET_CLI_TELEMETRY_OPTOUT=1 -e DOTNET_NOLOGO=1 \
    -v "$root:/workspace:rw" -w /workspace "$image" "$@"
