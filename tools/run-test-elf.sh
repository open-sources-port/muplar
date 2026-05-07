#!/bin/zsh

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ELF="$ROOT_DIR/build/bin/test_return_42"

echo "ROOT [${ROOT_DIR}]"
echo "ANDROID SDK [${ANDROID_NDK_HOME}]"

$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android21-clang \
  "$ROOT_DIR/tests/assets/elf/test_return_42.c" -static -o "$ROOT_DIR/build/bin/test_return_42"

if [ ! -f "$ELF" ]; then
    echo "ELF binary [$ELF] compile fai!"
    exit 1
fi

echo "Building the source code..."
echo
cmake --build build

echo "Running Muplar ELF loader..."
echo

"$ROOT_DIR/build/bin/mup" "$ELF"
