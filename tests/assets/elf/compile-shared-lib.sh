#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"

NDK_PREBUILT=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64
CC=$NDK_PREBUILT/bin/aarch64-linux-android35-clang

SYSROOT_TMP="$ROOT_DIR/build/sysroot/data/local/tmp"
SYSROOT_LIB="$ROOT_DIR/build/sysroot/system/lib64"

mkdir -p "$ROOT_DIR/build/bin" "$SYSROOT_TMP" "$SYSROOT_LIB"

# --- libadd.so ---
echo "[compile] Building libadd.so ..."
$CC -shared -fPIC -Wl,-z,max-page-size=4096 \
    "$ROOT_DIR/tests/assets/elf/libadd.c" \
    -o "$SYSROOT_TMP/libadd.so"

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
