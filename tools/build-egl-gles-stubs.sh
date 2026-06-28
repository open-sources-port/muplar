#!/bin/zsh
set -e

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
STUBS_DIR="${ROOT_DIR}/tools/guest-egl-gles-stubs"
OUTPUT_DIR="${BUILD_DIR}/bin"

if [ -z "$ANDROID_NDK_HOME" ]; then
    ANDROID_NDK_HOME="/opt/homebrew/share/android-ndk"
fi

if [ ! -d "$ANDROID_NDK_HOME" ]; then
    echo "ERROR: ANDROID_NDK_HOME is not set or does not exist at $ANDROID_NDK_HOME"
    exit 1
fi

NDK_PREBUILT="${ANDROID_NDK_HOME}/toolchains/llvm/prebuilt/darwin-x86_64"
if [ ! -d "$NDK_PREBUILT" ]; then
    echo "ERROR: NDK prebuilt directory not found at $NDK_PREBUILT"
    exit 1
fi

CC_ARM64=$(find "$NDK_PREBUILT/bin" -name "aarch64-linux-android*-clang" | head -n 1)
CC_X64=$(find "$NDK_PREBUILT/bin" -name "x86_64-linux-android*-clang" | head -n 1)
STRIP="${NDK_PREBUILT}/bin/llvm-strip"

if [ -z "$CC_ARM64" ] || [ ! -x "$CC_ARM64" ]; then
    echo "ERROR: AArch64 NDK compiler not found under $NDK_PREBUILT/bin"
    exit 1
fi

if [ -z "$CC_X64" ] || [ ! -x "$CC_X64" ]; then
    echo "ERROR: x86_64 NDK compiler not found under $NDK_PREBUILT/bin"
    exit 1
fi

mkdir -p "$OUTPUT_DIR/aarch64"
mkdir -p "$OUTPUT_DIR/x86_64"

echo "Compiling guest AArch64 libEGL.so..."
"$CC_ARM64" -shared -fPIC -nostdlib -Wl,-soname,libEGL.so.1 -o "$OUTPUT_DIR/aarch64/libEGL.so" "$STUBS_DIR/libEGL.c"
"$STRIP" "$OUTPUT_DIR/aarch64/libEGL.so"

echo "Compiling guest AArch64 libGLESv2.so..."
"$CC_ARM64" -shared -fPIC -nostdlib -Wl,-soname,libGLESv2.so.2 -o "$OUTPUT_DIR/aarch64/libGLESv2.so" "$STUBS_DIR/libGLESv2.c"
"$STRIP" "$OUTPUT_DIR/aarch64/libGLESv2.so"

echo "Compiling guest x86_64 libEGL.so..."
"$CC_X64" -shared -fPIC -nostdlib -Wl,-soname,libEGL.so.1 -o "$OUTPUT_DIR/x86_64/libEGL.so" "$STUBS_DIR/libEGL.c"
"$STRIP" "$OUTPUT_DIR/x86_64/libEGL.so"

echo "Compiling guest x86_64 libGLESv2.so..."
"$CC_X64" -shared -fPIC -nostdlib -Wl,-soname,libGLESv2.so.2 -o "$OUTPUT_DIR/x86_64/libGLESv2.so" "$STUBS_DIR/libGLESv2.c"
"$STRIP" "$OUTPUT_DIR/x86_64/libGLESv2.so"

echo "Successfully built guest AArch64 and x86_64 EGL/GLESv2 stubs."
