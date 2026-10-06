#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
JDK="$ROOT_DIR/third_party/jdk-bin/macos-aarch64/bin"
CLASSES="$ROOT_DIR/build/tests/launcher3-visual-classes"
SCREENSHOT="$ROOT_DIR/build/launcher3/verification/all-apps.png"
GUEST_SCREENSHOT="/data/local/tmp/muplar/launcher3-visual-smoke.png"
SYSROOT_SCREENSHOT="$HOME/.muplar/sysroots/android-arm64/api-35/sysroot$GUEST_SCREENSHOT"

mkdir -p "$CLASSES" "$(dirname "$SCREENSHOT")"
rm -f "$SCREENSHOT"
MUPLAR_LAUNCHER3_SCREENSHOT="$GUEST_SCREENSHOT" "$SCRIPT_DIR/smoke-launch.sh"
SCREENSHOT_PATH=""
for p in "$HOME/.muplar/prefixes/android-arm64/rootfs/data/local/tmp/muplar/launcher3-visual-smoke.png" \
         "$HOME/.muplar/sysroots/android-arm64/api-35/sysroot/data/local/tmp/muplar/launcher3-visual-smoke.png" \
         "/data/local/tmp/muplar/launcher3-visual-smoke.png"; do
    if [[ -f "$p" ]]; then
        SCREENSHOT_PATH="$p"
        break
    fi
done
if [[ -z "$SCREENSHOT_PATH" ]]; then
    echo "FAIL: Screenshot file not found in any path" >&2
    exit 1
fi
cp "$SCREENSHOT_PATH" "$SCREENSHOT"
LOG="${TMPDIR:-/tmp}/muplar-launcher3-smoke.log"
if grep -q "fallback screenshot written" "$LOG"; then
    echo "FAIL: Log indicates fallback screenshot was written instead of real Bitmap screenshot" >&2
    exit 1
fi

"$JDK/javac" -Xlint:-options --release 8 -d "$CLASSES" \
    "$ROOT_DIR/tests/android/PngVisualSmoke.java"
"$JDK/java" -cp "$CLASSES" PngVisualSmoke "$SCREENSHOT"
shasum -a 256 "$SCREENSHOT"
