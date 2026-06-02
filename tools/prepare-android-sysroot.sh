#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

SYSROOT="$ROOT_DIR/build/sysroot"
ANDROID_ROOT="${MUPLAR_ANDROID_ROOT:-}"
NDK_HOME="${ANDROID_NDK_HOME:-}"
IMPORT_ANDROID_ROOT=true
COPY_NDK_RUNTIME=true
BUILD_ART_BOOTSTRAP=true
STRICT_ART=false

usage() {
    echo "Usage: $0 [--sysroot PATH] [--android-root PATH|--from PATH] [--ndk PATH]"
    echo "          [--no-android-root] [--no-ndk-runtime] [--no-art-bootstrap] [--strict-art]"
    echo
    echo "Prepares Muplar's generated Android ARM64 sysroot from explicit local inputs."
    echo "Android runtime files come from --android-root or MUPLAR_ANDROID_ROOT."
    echo "NDK test runtime files come from --ndk or ANDROID_NDK_HOME."
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --sysroot)
            if [ "$#" -lt 2 ]; then
                echo "--sysroot requires a path" >&2
                exit 2
            fi
            SYSROOT="$2"
            shift 2
            ;;
        --android-root|--from)
            if [ "$#" -lt 2 ]; then
                echo "$1 requires a path" >&2
                exit 2
            fi
            ANDROID_ROOT="$2"
            IMPORT_ANDROID_ROOT=true
            shift 2
            ;;
        --ndk)
            if [ "$#" -lt 2 ]; then
                echo "--ndk requires a path" >&2
                exit 2
            fi
            NDK_HOME="$2"
            shift 2
            ;;
        --no-android-root|--skip-android-root)
            ANDROID_ROOT=""
            IMPORT_ANDROID_ROOT=false
            shift
            ;;
        --no-ndk-runtime|--skip-ndk-runtime)
            COPY_NDK_RUNTIME=false
            shift
            ;;
        --no-art-bootstrap|--skip-art-bootstrap)
            BUILD_ART_BOOTSTRAP=false
            shift
            ;;
        --strict-art)
            STRICT_ART=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

case "$SYSROOT" in
    /*) ;;
    *) SYSROOT="$ROOT_DIR/$SYSROOT" ;;
esac

mkdir -p "$SYSROOT/data/local/tmp" "$SYSROOT/system/lib64"

find_ndk_prebuilt() {
    local ndk="$1"
    if [ -z "$ndk" ] || [ ! -d "$ndk/toolchains/llvm/prebuilt" ]; then
        return 1
    fi

    local preferred
    for preferred in darwin-x86_64 darwin-arm64 linux-x86_64; do
        if [ -d "$ndk/toolchains/llvm/prebuilt/$preferred" ]; then
            echo "$ndk/toolchains/llvm/prebuilt/$preferred"
            return 0
        fi
    done

    find "$ndk/toolchains/llvm/prebuilt" -mindepth 1 -maxdepth 1 -type d |
        sort |
        head -1
}

if [ "$IMPORT_ANDROID_ROOT" = true ] && [ -n "$ANDROID_ROOT" ]; then
    if [ ! -d "$ANDROID_ROOT" ]; then
        echo "Android root not found: $ANDROID_ROOT" >&2
        exit 2
    fi
    echo "[sysroot] Importing Android ART root: $ANDROID_ROOT"
    "$ROOT_DIR/tools/import-android-art-sysroot.sh" \
        --from "$ANDROID_ROOT" \
        --sysroot "$SYSROOT"
elif [ "$STRICT_ART" = true ]; then
    echo "No Android root provided. Use --android-root or MUPLAR_ANDROID_ROOT." >&2
    exit 2
else
    echo "[sysroot] Android ART root: skipped"
fi

if [ "$COPY_NDK_RUNTIME" = true ]; then
    if [ -z "$NDK_HOME" ]; then
        echo "ANDROID_NDK_HOME is not set. Use --ndk or export ANDROID_NDK_HOME." >&2
        exit 2
    fi

    NDK_PREBUILT="$(find_ndk_prebuilt "$NDK_HOME" || true)"
    if [ -z "$NDK_PREBUILT" ]; then
        echo "Unable to find LLVM prebuilt under NDK: $NDK_HOME" >&2
        exit 2
    fi

    LIBCXX="$NDK_PREBUILT/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so"
    if [ ! -f "$LIBCXX" ]; then
        echo "NDK libc++_shared.so not found: $LIBCXX" >&2
        exit 2
    fi

    cp -p "$LIBCXX" "$SYSROOT/data/local/tmp/libc++_shared.so"
    echo "[sysroot] NDK runtime: data/local/tmp/libc++_shared.so"
else
    echo "[sysroot] NDK runtime: skipped"
fi

if [ "$BUILD_ART_BOOTSTRAP" = true ]; then
    "$ROOT_DIR/tools/build-art-bootstrap-jar.sh" --sysroot "$SYSROOT"
else
    echo "[sysroot] ART bootstrap jar: skipped"
fi

echo "[sysroot] Prepared: $SYSROOT"
