#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
OUT="$ROOT_DIR/build/sysroot/data/local/tmp/muplar/art/libmuplar_android_art_shim.so"

usage() {
    echo "Usage: $0 [--output PATH]"
    echo
    echo "Builds Muplar's ARM64 Android ART preload shim."
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --output)
            [ "$#" -ge 2 ] || { echo "--output requires a path" >&2; exit 2; }
            OUT="$2"
            shift 2
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

case "$OUT" in
    /*) ;;
    *) OUT="$ROOT_DIR/$OUT" ;;
esac

CC_BIN="${ANDROID_NDK_CLANG:-}"
if [ -z "$CC_BIN" ]; then
    NDK_HOME="${ANDROID_NDK_HOME:-}"
    for candidate in \
        "$NDK_HOME/toolchains/llvm/prebuilt/darwin-arm64/bin/aarch64-linux-android35-clang" \
        "$NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android35-clang" \
        "/opt/homebrew/share/android-ndk/toolchains/llvm/prebuilt/darwin-arm64/bin/aarch64-linux-android35-clang" \
        "/opt/homebrew/share/android-ndk/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android35-clang"; do
        if [ -n "$candidate" ] && [ -x "$candidate" ]; then
            CC_BIN="$candidate"
            break
        fi
    done
fi

if [ -z "$CC_BIN" ] || [ ! -x "$CC_BIN" ]; then
    echo "Android NDK clang not found. Set ANDROID_NDK_CLANG." >&2
    exit 1
fi

SRC="$ROOT_DIR/platform/android-aarch64/art-shim/muplar_android_art_shim.c"
mkdir -p "$(dirname "$OUT")"
"$CC_BIN" -shared -fPIC -Os -Wall -Wextra \
    -Wl,-soname,libmuplar_android_art_shim.so \
    -o "$OUT" "$SRC" -ldl

echo "[ART] Built Android ART shim: $OUT"
file "$OUT"
