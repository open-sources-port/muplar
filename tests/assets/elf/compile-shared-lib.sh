#!/bin/zsh
# compile-shared-lib.sh
# Builds libadd.so and test_shared for Android aarch64, then populates
# build/sysroot/ with the real Android runtime libraries from the NDK.
# No device needed — the NDK ships the prebuilt .so files.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"

NDK_PREBUILT=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64
CC=$NDK_PREBUILT/bin/aarch64-linux-android35-clang

# The NDK ships real aarch64 Android .so files here (not linker scripts).
# These are the same files that ship on an Android device.
NDK_LIB=$NDK_PREBUILT/sysroot/usr/lib/aarch64-linux-android/35

SYSROOT_TMP="$ROOT_DIR/build/sysroot/data/local/tmp"
SYSROOT_LIB="$ROOT_DIR/build/sysroot/system/lib64"

mkdir -p "$ROOT_DIR/build/bin" "$SYSROOT_TMP" "$SYSROOT_LIB"

# --- Populate sysroot with real NDK runtime libraries ---
# These are genuine Android aarch64 shared libraries from the NDK prebuilt.
echo "[compile] Copying Android runtime libs from NDK sysroot ..."
for lib in libc.so libm.so libdl.so libstdc++.so libc++_shared.so; do
    if [ -f "$NDK_LIB/$lib" ]; then
        cp "$NDK_LIB/$lib" "$SYSROOT_LIB/$lib"
        echo "[compile]   copied $lib"
    else
        echo "[compile]   WARNING: $lib not found in NDK at $NDK_LIB"
    fi
done

# --- libadd.so ---
echo "[compile] Building libadd.so ..."
$CC -shared -fPIC -Wl,-z,max-page-size=4096 \
    "$ROOT_DIR/tests/assets/elf/libadd.c" \
    -o "$SYSROOT_TMP/libadd.so"

# Also copy to lib64 so the standard search path finds it too
cp "$SYSROOT_TMP/libadd.so" "$SYSROOT_LIB/libadd.so"

# --- test_shared ---
echo "[compile] Building test_shared ..."
$CC -Wl,-z,max-page-size=4096 \
    "$ROOT_DIR/tests/assets/elf/test_shared.c" \
    -L "$SYSROOT_TMP" \
    -ladd \
    -Wl,-rpath,/data/local/tmp \
    -o "$ROOT_DIR/build/bin/test_shared"

echo "[compile] file $ROOT_DIR/build/bin/test_shared:"
file "$ROOT_DIR/build/bin/test_shared"

# --- linker64 ---
echo "[compile] Building linker64 ..."
chmod +x "$ROOT_DIR/linker64/build-linker64.sh"
"$ROOT_DIR/linker64/build-linker64.sh"
