#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
ANGLE_DYLIB_DIR="$ROOT_DIR/third_party/angle-bin"
if [ -d "$ANGLE_DYLIB_DIR" ]; then
    export DYLD_LIBRARY_PATH="$ANGLE_DYLIB_DIR${DYLD_LIBRARY_PATH:+:$DYLD_LIBRARY_PATH}"
fi

SYSROOT="$ROOT_DIR/build/sysroot"
MUP="$ROOT_DIR/build/bin/mup"
APK="$ROOT_DIR/build/sysroot/data/local/tmp/tinyjavaactivity.apk"
BUILD_APK=true

usage() {
    echo "Usage: $0 [--sysroot PATH] [--mup PATH] [--apk PATH] [--no-build-apk]"
    echo
    echo "Runs the tiny Java Activity fixture through app_process64."
    echo "Requires an ART-capable sysroot and d8 for a real classes.dex."
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --sysroot)
            if [ "$#" -lt 2 ]; then
                echo "--sysroot requires a path" >&2
                exit 2
            fi
            SYSROOT="$2"
            shift 2
            ;;
        --mup)
            if [ "$#" -lt 2 ]; then
                echo "--mup requires a path" >&2
                exit 2
            fi
            MUP="$2"
            shift 2
            ;;
        --apk)
            if [ "$#" -lt 2 ]; then
                echo "--apk requires a path" >&2
                exit 2
            fi
            APK="$2"
            BUILD_APK=false
            shift 2
            ;;
        --no-build-apk)
            BUILD_APK=false
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [ ! -x "$MUP" ]; then
    echo "mup binary not found or not executable: $MUP" >&2
    exit 2
fi

"$ROOT_DIR/tools/check-android-art-sysroot.sh" --sysroot "$SYSROOT"
"$ROOT_DIR/tools/build-art-bootstrap-jar.sh" --sysroot "$SYSROOT"

if [ "$BUILD_APK" = true ]; then
    "$ROOT_DIR/tests/assets/apk/create-tiny-java-activity-apk.sh" \
        --require-real-dex
fi

if [ ! -f "$APK" ]; then
    echo "APK not found: $APK" >&2
    exit 2
fi

"$MUP" --sysroot "$SYSROOT" "$APK"
