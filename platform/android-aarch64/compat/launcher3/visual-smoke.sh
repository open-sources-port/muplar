#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
JDK="$ROOT_DIR/third_party/jdk-bin/macos-aarch64/bin"
CLASSES="$ROOT_DIR/build/tests/launcher3-visual-classes"
SCREENSHOT="$ROOT_DIR/build/launcher3/verification/all-apps.png"

mkdir -p "$CLASSES" "$(dirname "$SCREENSHOT")"
rm -f "$SCREENSHOT"
MUPLAR_LAUNCHER3_SCREENSHOT="$SCREENSHOT" "$SCRIPT_DIR/smoke-launch.sh"
"$JDK/javac" -Xlint:-options --release 8 -d "$CLASSES" \
    "$ROOT_DIR/tests/android/PngVisualSmoke.java"
"$JDK/java" -cp "$CLASSES" PngVisualSmoke "$SCREENSHOT"
shasum -a 256 "$SCREENSHOT"
