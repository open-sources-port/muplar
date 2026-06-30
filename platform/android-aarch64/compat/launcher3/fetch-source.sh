#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
source "$SCRIPT_DIR/PIN.env"

DESTINATION="${1:-$ROOT_DIR/build/launcher3/source}"
if [ -d "$DESTINATION/.git" ]; then
    ACTUAL="$(git -C "$DESTINATION" rev-parse HEAD)"
    if [ "$ACTUAL" = "$LAUNCHER3_COMMIT" ]; then
        echo "[Launcher3] pinned source already ready: $DESTINATION"
        exit 0
    fi
    echo "Launcher3 source has unexpected revision: $ACTUAL" >&2
    echo "Expected: $LAUNCHER3_COMMIT" >&2
    exit 1
fi

mkdir -p "$(dirname "$DESTINATION")"
git clone --depth 1 --branch "$LAUNCHER3_TAG" \
    "$LAUNCHER3_REPOSITORY" "$DESTINATION"
ACTUAL="$(git -C "$DESTINATION" rev-parse HEAD)"
if [ "$ACTUAL" != "$LAUNCHER3_COMMIT" ]; then
    echo "Launcher3 pin mismatch: expected $LAUNCHER3_COMMIT, got $ACTUAL" >&2
    exit 1
fi
echo "[Launcher3] fetched $LAUNCHER3_TAG ($ACTUAL)"

