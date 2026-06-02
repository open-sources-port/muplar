#!/bin/zsh

set -u

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT_NAME="$0"
SYSROOT="$ROOT_DIR/build/sysroot"
SOURCE_ROOT=""
DRY_RUN=false

usage() {
    echo "Usage: $SCRIPT_NAME --from ANDROID_ROOT [--sysroot PATH] [--dry-run]"
    echo
    echo "Imports the ART-facing pieces of an extracted Android filesystem into"
    echo "Muplar's sysroot. ANDROID_ROOT may be a full root containing system/"
    echo "and apex/, a system/ directory, or an apex/ directory."
    echo
    echo "This does not download Android. Use it after extracting a device image,"
    echo "emulator image, or other Android filesystem tree you are allowed to use."
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --from|--source)
            if [ "$#" -lt 2 ]; then
                echo "$1 requires a path" >&2
                exit 2
            fi
            SOURCE_ROOT="$2"
            shift 2
            ;;
        --sysroot)
            if [ "$#" -lt 2 ]; then
                echo "--sysroot requires a path" >&2
                exit 2
            fi
            SYSROOT="$2"
            shift 2
            ;;
        --dry-run)
            DRY_RUN=true
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

if [ -z "$SOURCE_ROOT" ]; then
    echo "--from is required" >&2
    usage >&2
    exit 2
fi

if [ ! -d "$SOURCE_ROOT" ]; then
    echo "source root not found: $SOURCE_ROOT" >&2
    exit 2
fi

find_source_path() {
    local rel="$1"

    if [ -e "$SOURCE_ROOT/$rel" ]; then
        echo "$SOURCE_ROOT/$rel"
        return 0
    fi

    if [[ "$rel" == system/* ]] && [ -e "$SOURCE_ROOT/${rel#system/}" ]; then
        echo "$SOURCE_ROOT/${rel#system/}"
        return 0
    fi

    if [[ "$rel" == apex/* ]] && [ -e "$SOURCE_ROOT/${rel#apex/}" ]; then
        echo "$SOURCE_ROOT/${rel#apex/}"
        return 0
    fi

    return 1
}

copy_path() {
    local rel="$1"
    local src
    src="$(find_source_path "$rel" || true)"
    if [ -z "$src" ]; then
        return 1
    fi

    local dst="$SYSROOT/$rel"
    echo "import: $rel"
    if [ "$DRY_RUN" = true ]; then
        echo "  $src -> $dst"
        return 0
    fi

    mkdir -p "$(dirname "$dst")"
    if [ -d "$src" ]; then
        mkdir -p "$dst"
        if command -v ditto >/dev/null 2>&1; then
            ditto "$src" "$dst"
        else
            cp -Rp "$src/." "$dst/"
        fi
    else
        cp -p "$src" "$dst"
    fi
    return 0
}

copy_first_required() {
    local label="$1"
    shift

    local rel
    for rel in "$@"; do
        if copy_path "$rel"; then
            return 0
        fi
    done

    echo "missing required source path for $label" >&2
    printf "  tried:" >&2
    for rel in "$@"; do
        printf " %s" "$rel" >&2
    done
    printf "\n" >&2
    return 1
}

copy_optional() {
    local rel="$1"
    copy_path "$rel" >/dev/null || true
}

mkdir -p "$SYSROOT"

missing=0
copy_first_required "app_process64" \
    system/bin/app_process64 \
    apex/com.android.art/bin/app_process64 || missing=$((missing + 1))
copy_first_required "core-oj.jar" \
    apex/com.android.art/javalib/core-oj.jar \
    system/framework/core-oj.jar || missing=$((missing + 1))
copy_first_required "core-libart.jar" \
    apex/com.android.art/javalib/core-libart.jar \
    system/framework/core-libart.jar || missing=$((missing + 1))
copy_first_required "framework.jar" \
    system/framework/framework.jar || missing=$((missing + 1))
copy_first_required "libandroid_runtime.so" \
    system/lib64/libandroid_runtime.so || missing=$((missing + 1))
copy_first_required "libart.so" \
    apex/com.android.art/lib64/libart.so \
    system/lib64/libart.so || missing=$((missing + 1))

# Useful support directories. Missing optional directories are ignored because
# Android image layouts vary by version and vendor.
copy_optional system/framework
copy_optional system/lib64
copy_optional system/etc
copy_optional apex/com.android.art/bin
copy_optional apex/com.android.art/javalib
copy_optional apex/com.android.art/lib64
copy_optional apex/com.android.runtime/lib64
copy_optional apex/com.android.conscrypt/javalib
copy_optional apex/com.android.i18n/javalib
copy_optional apex/com.android.tzdata

if [ "$missing" -ne 0 ]; then
    echo "ART sysroot import incomplete: $missing required input(s) missing" >&2
    exit 1
fi

if [ "$DRY_RUN" = true ]; then
    echo "dry-run complete"
    exit 0
fi

"$ROOT_DIR/tools/check-android-art-sysroot.sh" --sysroot "$SYSROOT"
