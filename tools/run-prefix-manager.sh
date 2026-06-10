#!/bin/zsh
set -e

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

if [ ! -f "$ROOT_DIR/build/CMakeCache.txt" ]; then
  CMAKE_ARGS=(-S "$ROOT_DIR" -B "$ROOT_DIR/build" -G Ninja)
  if command -v "$NINJA_BIN" >/dev/null 2>&1; then
    CMAKE_ARGS+=(-DCMAKE_MAKE_PROGRAM="$NINJA_BIN")
  fi
  "$CMAKE_BIN" "${CMAKE_ARGS[@]}"
else
  "$CMAKE_BIN" -S "$ROOT_DIR" -B "$ROOT_DIR/build"
fi

"$CMAKE_BIN" --build "$ROOT_DIR/build" --target populate_manager_bundle -j4

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
