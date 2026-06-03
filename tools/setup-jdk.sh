#!/bin/zsh
set -euo pipefail

# ---------------------------------------------------------------------------
# tools/setup-jdk.sh
#
# Downloads Eclipse Temurin 21 (LTS) for macOS and extracts only what
# Muplar needs:
#   third_party/jdk-bin/macos-aarch64/lib/server/libjvm.dylib
#   third_party/jdk-bin/macos-aarch64/include/jni.h
#   third_party/jdk-bin/macos-aarch64/include/darwin/jni_md.h
#   (same layout for macos-x86_64)
#
# Usage:
#   tools/setup-jdk.sh                    # auto-detect arch
#   tools/setup-jdk.sh --arch aarch64     # Apple Silicon
#   tools/setup-jdk.sh --arch x86_64      # Intel
#   tools/setup-jdk.sh --jdk-version 21   # Temurin major version (default: 21)
#   tools/setup-jdk.sh --force            # re-download even if already present
# ---------------------------------------------------------------------------

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

JDK_VERSION=21
FORCE=false
ARCH=""

usage() {
    echo "Usage: $0 [--arch aarch64|x86_64] [--jdk-version N] [--force]"
    echo
    echo "Downloads Eclipse Temurin JDK and installs it to third_party/jdk-bin/."
    echo "Run once before building Muplar. Safe to re-run."
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --arch)
            if [ "$#" -lt 2 ]; then echo "--arch requires aarch64 or x86_64" >&2; exit 2; fi
            ARCH="$2"; shift 2 ;;
        --jdk-version)
            if [ "$#" -lt 2 ]; then echo "--jdk-version requires a number" >&2; exit 2; fi
            JDK_VERSION="$2"; shift 2 ;;
        --force)
            FORCE=true; shift ;;
        -h|--help)
            usage; exit 0 ;;
        *)
            echo "Unknown argument: $1" >&2; usage >&2; exit 2 ;;
    esac
done

# Auto-detect architecture
if [ -z "$ARCH" ]; then
    machine="$(uname -m)"
    case "$machine" in
        arm64|aarch64) ARCH="aarch64" ;;
        x86_64)        ARCH="x86_64"  ;;
        *)
            echo "Unsupported architecture: $machine" >&2
            exit 1 ;;
    esac
fi

case "$ARCH" in
    aarch64) TEMURIN_OS_ARCH="aarch64"; JDK_BIN_SUBDIR="macos-aarch64" ;;
    x86_64)  TEMURIN_OS_ARCH="x64";     JDK_BIN_SUBDIR="macos-x86_64"  ;;
    *)
        echo "Unknown arch: $ARCH (use aarch64 or x86_64)" >&2
        exit 1 ;;
esac

JDK_OUT_DIR="$ROOT_DIR/third_party/jdk-bin/$JDK_BIN_SUBDIR"
LIBJVM_PATH="$JDK_OUT_DIR/lib/server/libjvm.dylib"
JNI_H_PATH="$JDK_OUT_DIR/include/jni.h"

# Check if already set up
if [ "$FORCE" = false ] && [ -f "$LIBJVM_PATH" ] && [ -f "$JNI_H_PATH" ]; then
    echo "[setup-jdk] Already installed: $JDK_OUT_DIR"
    echo "[setup-jdk] libjvm: $LIBJVM_PATH"
    echo "[setup-jdk] jni.h:  $JNI_H_PATH"
    echo "[setup-jdk] Use --force to re-download."
    exit 0
fi

# ---------------------------------------------------------------------------
# Determine download URL (Eclipse Temurin API)
# https://api.adoptium.net/v3/assets/latest/{version}/hotspot
# ---------------------------------------------------------------------------
TEMURIN_API="https://api.adoptium.net/v3/assets/latest/${JDK_VERSION}/hotspot"
TEMURIN_PARAMS="architecture=${TEMURIN_OS_ARCH}&image_type=jdk&os=mac&vendor=eclipse"
TEMURIN_URL="${TEMURIN_API}?${TEMURIN_PARAMS}"

echo "[setup-jdk] Fetching Temurin ${JDK_VERSION} download info for macOS ${ARCH}..."

# Get download URL from Adoptium API
if command -v curl >/dev/null 2>&1; then
    API_RESPONSE="$(curl -fsSL "$TEMURIN_URL")"
else
    echo "curl is required" >&2
    exit 1
fi

# Parse the .tar.gz download link (the binary_link field)
DOWNLOAD_URL="$(echo "$API_RESPONSE" | python3 -c "
import json, sys
data = json.load(sys.stdin)
# Find the .tar.gz asset (not .pkg)
for item in data:
    pkg = item.get('binary', {}).get('package', {})
    link = pkg.get('link', '')
    if link.endswith('.tar.gz'):
        print(link)
        break
" 2>/dev/null || true)"

if [ -z "$DOWNLOAD_URL" ]; then
    echo "[setup-jdk] Failed to parse download URL from Adoptium API." >&2
    echo "[setup-jdk] API URL: $TEMURIN_URL" >&2
    echo "$API_RESPONSE" | head -5 >&2
    exit 1
fi

# ---------------------------------------------------------------------------
# Cache downloaded tarball in .cache/
# ---------------------------------------------------------------------------
CACHE_DIR="$ROOT_DIR/.cache"
mkdir -p "$CACHE_DIR"

# Derive a stable cache filename from the download URL
TARBALL_NAME="$(basename "$DOWNLOAD_URL")"
TARBALL="$CACHE_DIR/$TARBALL_NAME"
if [ -f "$TARBALL" ] && [ "$FORCE" = false ]; then
    echo "[setup-jdk] Using cached: $TARBALL"
else
    echo "[setup-jdk] Downloading: $DOWNLOAD_URL"
    curl -fL --progress-bar -o "$TARBALL" "$DOWNLOAD_URL"
fi

echo "[setup-jdk] Extracting..."
EXTRACT_DIR="$TMPDIR_WORK/extracted"
mkdir -p "$EXTRACT_DIR"
tar -xzf "$TARBALL" -C "$EXTRACT_DIR"

# Find the JDK root (Contents/Home on macOS)
JDK_ROOT="$(find "$EXTRACT_DIR" -maxdepth 5 -name "jni.h" 2>/dev/null | head -1 | xargs -I{} dirname {} | xargs -I{} dirname {} || true)"
if [ -z "$JDK_ROOT" ]; then
    # macOS .tar.gz layout: <name>.jdk/Contents/Home/
    JDK_ROOT="$(find "$EXTRACT_DIR" -maxdepth 4 -type d -name "Home" | head -1 || true)"
fi
if [ -z "$JDK_ROOT" ]; then
    echo "[setup-jdk] Could not locate JDK root in extracted archive." >&2
    ls "$EXTRACT_DIR" >&2
    exit 1
fi

echo "[setup-jdk] JDK root: $JDK_ROOT"

# ---------------------------------------------------------------------------
# Copy only what Muplar needs
# ---------------------------------------------------------------------------
mkdir -p "$JDK_OUT_DIR/include/darwin"

# Copy the full lib/ directory — JNI_CreateJavaVM needs lib/modules (JDK9+
# boot image), lib/jli/libjli.dylib, lib/libjava.dylib, etc.
# We skip src.zip and man to save space.
echo "[setup-jdk] copying lib/ (this is ~80MB)..."
rsync -a --quiet \
    --exclude "src.zip" \
    "$JDK_ROOT/lib/" "$JDK_OUT_DIR/lib/"

# release file (contains JDK version metadata, needed by libjvm startup)
if [ -f "$JDK_ROOT/../release" ]; then
    cp "$JDK_ROOT/../release" "$JDK_OUT_DIR/release"
elif [ -f "$JDK_ROOT/release" ]; then
    cp "$JDK_ROOT/release" "$JDK_OUT_DIR/release"
fi

# jni.h
cp "$JDK_ROOT/include/jni.h" "$JDK_OUT_DIR/include/jni.h"

# jni_md.h (macOS-specific)
if [ -f "$JDK_ROOT/include/darwin/jni_md.h" ]; then
    cp "$JDK_ROOT/include/darwin/jni_md.h" "$JDK_OUT_DIR/include/darwin/jni_md.h"
elif [ -f "$JDK_ROOT/include/jni_md.h" ]; then
    cp "$JDK_ROOT/include/jni_md.h" "$JDK_OUT_DIR/include/darwin/jni_md.h"
fi


echo
echo "[setup-jdk] ✅ Installed Temurin ${JDK_VERSION} for macOS ${ARCH}:"
echo "   $JDK_OUT_DIR/"
ls -lh "$JDK_OUT_DIR/lib/server/libjvm.dylib"
ls -lh "$JDK_OUT_DIR/include/jni.h"
echo
echo "[setup-jdk] Now run: cd build && cmake .. && ninja"
