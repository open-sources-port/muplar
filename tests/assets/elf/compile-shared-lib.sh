#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"

NDK_PREBUILT=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64
CC=$NDK_PREBUILT/bin/aarch64-linux-android35-clang
NDK_SYSROOT=$NDK_PREBUILT/sysroot
SYSROOT_TMP="$ROOT_DIR/build/sysroot/data/local/tmp"
SYSROOT_LIB="$ROOT_DIR/build/sysroot/system/lib64"

mkdir -p "$ROOT_DIR/build/bin" "$SYSROOT_TMP" "$SYSROOT_LIB"

# --- libadd.so ---
echo "[compile] Building libadd.so ..."
$CC -shared -fPIC -Wl,-z,max-page-size=4096 \
    "$ROOT_DIR/tests/assets/elf/libadd.c" \
    -o "$SYSROOT_TMP/libadd.so"

# --- libjnitest.so ---


echo "[compile] Building libjnitest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libjnitest.c" \
    -llog \
    -o "$SYSROOT_TMP/libjnitest.so"

echo "[compile] Built: $SYSROOT_TMP/libjnitest.so"
file "$SYSROOT_TMP/libjnitest.so"

# --- libnativeactivitytest.so ---
echo "[compile] Building libnativeactivitytest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libnativeactivitytest.c" \
    -lEGL \
    -lGLESv2 \
    -landroid \
    -llog \
    -o "$SYSROOT_TMP/libnativeactivitytest.so"

echo "[compile] Built: $SYSROOT_TMP/libnativeactivitytest.so"
file "$SYSROOT_TMP/libnativeactivitytest.so"

# --- libnativegluethreadtest.so ---
echo "[compile] Building libnativegluethreadtest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libnativegluethreadtest.c" \
    -landroid \
    -llog \
    -o "$SYSROOT_TMP/libnativegluethreadtest.so"

echo "[compile] Built: $SYSROOT_TMP/libnativegluethreadtest.so"
file "$SYSROOT_TMP/libnativegluethreadtest.so"

# --- libnativeappgluecmdtest.so ---
echo "[compile] Building libnativeappgluecmdtest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libnativeappgluecmdtest.c" \
    -landroid \
    -llog \
    -o "$SYSROOT_TMP/libnativeappgluecmdtest.so"

echo "[compile] Built: $SYSROOT_TMP/libnativeappgluecmdtest.so"
file "$SYSROOT_TMP/libnativeappgluecmdtest.so"

# --- APK-local dependency fixture ---
echo "[compile] Building libapkdephelper.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    "$ROOT_DIR/tests/assets/elf/libapkdephelper.c" \
    -o "$SYSROOT_TMP/libapkdephelper.so"

echo "[compile] Built: $SYSROOT_TMP/libapkdephelper.so"
file "$SYSROOT_TMP/libapkdephelper.so"

echo "[compile] Building libapkdeptest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libapkdeptest.c" \
    -L "$SYSROOT_TMP" \
    -lapkdephelper \
    -landroid \
    -llog \
    -o "$SYSROOT_TMP/libapkdeptest.so"

echo "[compile] Built: $SYSROOT_TMP/libapkdeptest.so"
file "$SYSROOT_TMP/libapkdeptest.so"

# --- APK asset fixture ---
echo "[compile] Building libassettest.so ..."
$CC -shared -fPIC \
    -Wl,-z,max-page-size=4096 \
    -isystem "$NDK_SYSROOT/usr/include" \
    -isystem "$NDK_SYSROOT/usr/include/aarch64-linux-android" \
    "$ROOT_DIR/tests/assets/elf/libassettest.c" \
    -landroid \
    -llog \
    -o "$SYSROOT_TMP/libassettest.so"

echo "[compile] Built: $SYSROOT_TMP/libassettest.so"
file "$SYSROOT_TMP/libassettest.so"

# --- test_shared ---
# -nostartfiles: skip crtbegin_dynamic.o which calls __libc_init (needs real libc)
# We supply our own _start in test_shared_start.S that calls main() directly.
echo "[compile] Building test_shared ..."
$CC -Wl,-z,max-page-size=4096 \
    -nostartfiles \
    "$ROOT_DIR/tests/assets/elf/test_shared_start.S" \
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
