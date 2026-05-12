#!/bin/zsh
# build-linker64.sh — builds muplar's custom linker64 using the Android NDK
#
# Output: build/sysroot/system/bin/linker64
#
# The binary is a freestanding AArch64 ELF (ET_DYN, PIE) that elfuse loads
# as the PT_INTERP interpreter.  It has NO libc dependency.
#
# Why -pie and not -shared:
#   With -shared the toolchain leaves e_entry=0 even when -e _start is given.
#   elfuse computes the guest entry as (e_entry + interp_base), so e_entry=0
#   makes it jump to the raw interp_base address (the ELF header) — instant
#   fault.  With -pie the toolchain honours -e _start and writes the correct
#   offset into e_entry.
#
# Why --dynamic-linker='':
#   A -pie binary normally gets a PT_INTERP injected pointing back to
#   /system/bin/linker64.  We strip that to avoid infinite recursion
#   (elfuse loading linker64 to interpret linker64).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

CC=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android35-clang
READELF=$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin/llvm-readelf

SRC=$ROOT_DIR/linker64/src
BUILD=$ROOT_DIR/build/linker64
OUT=$ROOT_DIR/build/sysroot/system/bin

mkdir -p "$BUILD" "$OUT"

CLANG_VER=$(ls "$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/lib/clang")
CLANG_INC="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/lib/clang/$CLANG_VER/include"

CFLAGS=(
    -target aarch64-linux-android35
    -O2
    -ffreestanding
    -fno-stack-protector
    -fno-builtin
    -fPIC
    -nostdlib
    -nostdinc
    -isystem "$CLANG_INC"
    -I "$SRC"
)

echo "[linker64] Compiling entry.S ..."
$CC "${CFLAGS[@]}" -c "$SRC/entry.S" -o "$BUILD/entry.o"

echo "[linker64] Compiling linker.c ..."
$CC "${CFLAGS[@]}" -c "$SRC/linker.c" -o "$BUILD/linker.o"

echo "[linker64] Linking ..."
$CC \
    -target aarch64-linux-android35 \
    -nostdlib \
    -nostartfiles \
    -pie \
    -Wl,--no-undefined \
    -Wl,-z,max-page-size=4096 \
    -Wl,-e,_start \
    -Wl,--dynamic-linker='' \
    -Wl,--build-id=none \
    -Wl,-z,norelro \
    "$BUILD/entry.o" \
    "$BUILD/linker.o" \
    -o "$OUT/linker64"

echo "[linker64] Built: $OUT/linker64"

echo "[linker64] ELF header (Entry point must be non-zero):"
$READELF -h "$OUT/linker64" | grep -E "Type|Entry|Machine"

echo "[linker64] Checking for PT_INTERP (must be absent):"
INTERP=$($READELF -l "$OUT/linker64" | grep "program interpreter" || true)
if [ -n "$INTERP" ]; then
    echo "[linker64] ERROR: binary still has PT_INTERP: $INTERP"
    exit 1
fi
echo "[linker64] OK: no PT_INTERP"

echo "[linker64] Verifying no external DT_NEEDED:"
$READELF -d "$OUT/linker64" | grep NEEDED || echo "[linker64] OK: no DT_NEEDED"
