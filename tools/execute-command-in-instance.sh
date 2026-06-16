#!/bin/zsh
set -euo pipefail

INSTANCE_NAME="${1:-arch-arm64}"

# Remove instance name from arguments
if (( $# > 0 )); then
    shift
fi

# Default command
if (( $# == 0 )); then
    CMD=(ls -ail)
else
    CMD=("$@")
fi

ELFUSE_GUEST_UID=0 ELFUSE_GUEST_GID=0 \
build/bin/mup --prefix "$INSTANCE_NAME" "${CMD[@]}"
