#!/bin/zsh
# Generate NDK (--lang=ndk) C++ binder sources from tests/assets/aidl/real/*.aidl.
# Requires the Android SDK build-tools `aidl` binary (API 31+).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../.." && pwd)"
AIDL_SRC="$SCRIPT_DIR/real"
CPP_OUT="$SCRIPT_DIR/generated-ndk/cpp"
HDR_OUT="$SCRIPT_DIR/generated-ndk/include"

find_aidl() {
    if [[ -n "${AIDL:-}" && -x "${AIDL}" ]]; then
        echo "$AIDL"
        return 0
    fi
    local candidate
    for candidate in \
        "$ANDROID_HOME/build-tools/"*/aidl(N) \
        "$ANDROID_SDK_ROOT/build-tools/"*/aidl(N) \
        "$HOME/Library/Android/sdk/build-tools/"*/aidl(N); do
        if [[ -x "$candidate" ]]; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

AIDL_BIN="$(find_aidl)" || {
    echo "[aidl] error: could not find SDK build-tools aidl." >&2
    echo "[aidl] Install Android SDK build-tools or set AIDL=/path/to/aidl" >&2
    exit 1
}

echo "[aidl] Using: $AIDL_BIN"
mkdir -p "$CPP_OUT" "$HDR_OUT"

"$AIDL_BIN" --lang=ndk \
    -o "$CPP_OUT" \
    -h "$HDR_OUT" \
    -I "$AIDL_SRC" \
    "$AIDL_SRC/com/example/muplar/IRealAdder.aidl"

echo "[aidl] Generated NDK sources under $SCRIPT_DIR/generated-ndk/"
