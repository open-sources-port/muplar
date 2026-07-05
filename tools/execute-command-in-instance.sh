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

build/bin/mup --fakeroot --prefix "$INSTANCE_NAME" "${CMD[@]}"
