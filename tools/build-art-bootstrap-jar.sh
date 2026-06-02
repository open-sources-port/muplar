#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

SYSROOT="$ROOT_DIR/build/sysroot"
OUT=""

usage() {
    echo "Usage: $0 [--sysroot PATH] [--output PATH]"
    echo
    echo "Builds the Muplar Java bootstrap jar used by app_process64."
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
        --output)
            if [ "$#" -lt 2 ]; then
                echo "--output requires a path" >&2
                exit 2
            fi
            OUT="$2"
            shift 2
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

if [ -z "$OUT" ]; then
    OUT="$SYSROOT/data/local/tmp/muplar/art/muplar-art-bootstrap.jar"
fi

case "$SYSROOT" in
    /*) ;;
    *) SYSROOT="$ROOT_DIR/$SYSROOT" ;;
esac

case "$OUT" in
    /*) ;;
    *) OUT="$ROOT_DIR/$OUT" ;;
esac

SRC_DIR="$ROOT_DIR/tests/assets/java/art-bootstrap"
BUILD_DIR="$ROOT_DIR/build/java/art-bootstrap"
CLASSES_DIR="$BUILD_DIR/classes"
SOURCE_FILE="$SRC_DIR/com/muplar/runtime/ArtApkMain.java"

JAVAC_BIN="${JAVAC:-javac}"
JAR_BIN="${JAR:-jar}"

if ! command -v "$JAVAC_BIN" >/dev/null 2>&1; then
    echo "javac not found. Source /etc/share-environment.sh or set JAVAC." >&2
    exit 1
fi

if ! command -v "$JAR_BIN" >/dev/null 2>&1; then
    echo "jar not found. Source /etc/share-environment.sh or set JAR." >&2
    exit 1
fi

rm -rf "$CLASSES_DIR"
mkdir -p "$CLASSES_DIR" "$(dirname "$OUT")"

if "$JAVAC_BIN" --help 2>&1 | grep -q -- '--release'; then
    "$JAVAC_BIN" -Xlint:-options --release 8 -d "$CLASSES_DIR" "$SOURCE_FILE"
else
    "$JAVAC_BIN" -Xlint:-options -source 8 -target 8 \
        -d "$CLASSES_DIR" "$SOURCE_FILE"
fi

(cd "$CLASSES_DIR" && "$JAR_BIN" cf "$OUT" com)

echo "[ART] Built bootstrap jar: $OUT"
file "$OUT"
