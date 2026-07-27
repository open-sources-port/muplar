#!/bin/zsh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"
JOBS="${JOBS:-$(sysctl -n hw.ncpu 2>/dev/null || echo 4)}"

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

if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
  CMAKE_ARGS=(-S "$ROOT_DIR" -B "$BUILD_DIR" -G Ninja)
  if command -v ninja >/dev/null 2>&1; then
    CMAKE_ARGS+=(-DCMAKE_MAKE_PROGRAM="$(command -v ninja)")
  fi
  "$CMAKE_BIN" "${CMAKE_ARGS[@]}"
else
  "$CMAKE_BIN" -S "$ROOT_DIR" -B "$BUILD_DIR"
fi

"$CMAKE_BIN" --build "$BUILD_DIR" --target populate_manager_bundle -j"$JOBS"

APP="$ROOT_DIR/build/bin/Muplar Instance Manager.app"
APP_NAME="Muplar Instance Manager"

if pgrep -f "$APP/Contents/MacOS/$APP_NAME" >/dev/null 2>&1; then
  osascript -e "tell application \"$APP_NAME\" to quit" >/dev/null 2>&1 || true
  for _ in {1..20}; do
    if ! pgrep -f "$APP/Contents/MacOS/$APP_NAME" >/dev/null 2>&1; then
      break
    fi
    sleep 0.1
  done
fi

open -n "$APP"
