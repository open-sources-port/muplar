#!/bin/zsh

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
ELF="$ROOT_DIR/build/bin/test_return_42"

echo "ROOT [${ROOT_DIR}]"
echo "ANDROID SDK [${ANDROID_NDK_HOME}]"

echo "Building the source code..."
echo
cmake --build build

sh $ROOT_DIR/tests/assets/elf/compile.sh

echo "Running Muplar ELF loader..."
echo

"$ROOT_DIR/build/bin/mup" "$ELF"
