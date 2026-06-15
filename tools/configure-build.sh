#!/bin/zsh

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
CMAKE_BIN="${CMAKE_BIN:-cmake}"
if ! command -v "$CMAKE_BIN" >/dev/null 2>&1; then
  if [ -x /opt/homebrew/bin/cmake ]; then
    CMAKE_BIN=/opt/homebrew/bin/cmake
  elif [ -x /Applications/CMake.app/Contents/bin/cmake ]; then
    CMAKE_BIN=/Applications/CMake.app/Contents/bin/cmake
  fi
fi

if ! command -v "$CMAKE_BIN" >/dev/null 2>&1; then
  echo "cmake not found. Set CMAKE_BIN or install CMake." >&2
  exit 1
fi

NINJA_BIN="${NINJA_BIN:-ninja}"
if ! command -v "$NINJA_BIN" >/dev/null 2>&1; then
  if [ -x /opt/homebrew/bin/ninja ]; then
    NINJA_BIN=/opt/homebrew/bin/ninja
  elif [ -x /usr/local/bin/ninja ]; then
    NINJA_BIN=/usr/local/bin/ninja
  fi
fi

if ! command -v "$NINJA_BIN" >/dev/null 2>&1; then
  echo "ninja not found. Set NINJA_BIN or install Ninja." >&2
  exit 1
fi

NDK_SYSROOT=$NDK_PREBUILT/sysroot
SYSROOT_TMP="$ROOT_DIR/build/sysroot/data/local/tmp"
SYSROOT_LIB="$ROOT_DIR/build/sysroot/system/lib64"

echo "Configuring source code..."
"$CMAKE_BIN" -S "$ROOT_DIR" -B "$ROOT_DIR/build" -G Ninja -DCMAKE_MAKE_PROGRAM="$NINJA_BIN"
echo "Configuring source code done."
