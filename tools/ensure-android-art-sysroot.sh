#!/bin/zsh
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SCRIPT_NAME="$0"

DEFAULT_ID="1Te7UUvVLR-nQppORbVLlCqmpPNUEIQU3"
DEFAULT_URL="https://drive.google.com/file/d/${DEFAULT_ID}/view?usp=sharing"

SYSROOT="${MUPLAR_ANDROID_SYSROOT:-$HOME/.muplar/sysroots/android-arm64/api-35/sysroot}"
CACHE_DIR="${MUPLAR_ANDROID_SYSROOT_CACHE:-$HOME/.muplar/cache/android-sysroot}"
SOURCE_URL="${MUPLAR_ANDROID_SYSROOT_URL:-$DEFAULT_URL}"
SOURCE_FILE_ID="${MUPLAR_ANDROID_SYSROOT_FILE_ID:-$DEFAULT_ID}"
BOOTSTRAP_JAR="${MUPLAR_ART_BOOTSTRAP_JAR:-}"
ARCHIVE=""
FORCE=false
PRINT_PATH=false

usage() {
    echo "Usage: $SCRIPT_NAME [--sysroot PATH] [--url URL] [--file-id ID]"
    echo "       $SCRIPT_NAME [--sysroot PATH] --archive PATH"
    echo "       $SCRIPT_NAME --print-path"
    echo
    echo "Ensures a production Android ARM64 ART sysroot exists in Muplar's shared"
    echo "cache. The artifact may be a prepared Muplar sysroot or an extracted"
    echo "Android root containing system/ and apex/."
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --sysroot)
            [ "$#" -ge 2 ] || { echo "--sysroot requires a path" >&2; exit 2; }
            SYSROOT="$2"
            shift 2
            ;;
        --url)
            [ "$#" -ge 2 ] || { echo "--url requires a value" >&2; exit 2; }
            SOURCE_URL="$2"
            shift 2
            ;;
        --file-id)
            [ "$#" -ge 2 ] || { echo "--file-id requires a value" >&2; exit 2; }
            SOURCE_FILE_ID="$2"
            shift 2
            ;;
        --archive)
            [ "$#" -ge 2 ] || { echo "--archive requires a path" >&2; exit 2; }
            ARCHIVE="$2"
            shift 2
            ;;
        --force)
            FORCE=true
            shift
            ;;
        --print-path)
            PRINT_PATH=true
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
    ~/*) SYSROOT="$HOME/${SYSROOT#~/}" ;;
esac

if [ "$PRINT_PATH" = true ]; then
    echo "$SYSROOT"
    exit 0
fi

mkdir -p "$CACHE_DIR" "$(dirname "$SYSROOT")"

detect_extension() {
    local path="$1"
    case "$path" in
        *.tar.gz|*.tgz) echo "tar.gz" ;;
        *.tar.xz|*.txz) echo "tar.xz" ;;
        *.tar.bz2|*.tbz2) echo "tar.bz2" ;;
        *.tar) echo "tar" ;;
        *.zip) echo "zip" ;;
        *) echo "tar.gz" ;;
    esac
}

if [ -z "$ARCHIVE" ]; then
    ext="$(detect_extension "$SOURCE_URL")"
    ARCHIVE="$CACHE_DIR/android-arm64-api35-sysroot.$ext"
fi

LOCK_DIR="$CACHE_DIR/.ensure.lock"
LOCKED=false
while ! mkdir "$LOCK_DIR" 2>/dev/null; do
    echo "[android-sysroot] waiting for another sysroot setup to finish..."
    sleep 2
done
LOCKED=true
cleanup() {
    if [ "$LOCKED" = true ]; then
        rm -rf "$LOCK_DIR"
    fi
}
trap cleanup EXIT INT TERM

ensure_linker_aliases() {
    local system_bin="$SYSROOT/system/bin"
    mkdir -p "$system_bin"

    if [ ! -e "$system_bin/linker64" ] &&
       [ -f "$SYSROOT/apex/com.android.runtime/bin/linker64" ]; then
        ln -s ../../apex/com.android.runtime/bin/linker64 \
            "$system_bin/linker64"
        echo "[android-sysroot] linked /system/bin/linker64"
    fi

    if [ ! -e "$system_bin/linker" ] &&
       [ -f "$SYSROOT/apex/com.android.runtime/bin/linker" ]; then
        ln -s ../../apex/com.android.runtime/bin/linker \
            "$system_bin/linker"
        echo "[android-sysroot] linked /system/bin/linker"
    fi
}

install_bootstrap_jar() {
    local out="$SYSROOT/data/local/tmp/muplar/art/muplar-art-bootstrap.jar"
    local candidates=()

    if [ -n "$BOOTSTRAP_JAR" ]; then
        candidates+=("$BOOTSTRAP_JAR")
    fi

    candidates+=(
        "$ROOT_DIR/android/muplar-art-bootstrap.jar"
        "$ROOT_DIR/build/sysroot/data/local/tmp/muplar/art/muplar-art-bootstrap.jar"
    )

    local candidate
    for candidate in "${candidates[@]}"; do
        if [ -f "$candidate" ]; then
            mkdir -p "$(dirname "$out")"
            cp -f "$candidate" "$out"
            echo "[android-sysroot] installed Muplar bootstrap jar: $out"
            return 0
        fi
    done

    "$ROOT_DIR/tools/build-art-bootstrap-jar.sh" --sysroot "$SYSROOT"
}

if [ "$FORCE" = false ] && [ -d "$SYSROOT" ]; then
    ensure_linker_aliases
    install_bootstrap_jar
fi

if [ "$FORCE" = false ] &&
   "$ROOT_DIR/tools/check-android-art-sysroot.sh" --sysroot "$SYSROOT" --quiet >/dev/null 2>&1 &&
   [ -s "$ARCHIVE" ]; then
    echo "[android-sysroot] ready: $SYSROOT"
    echo "[android-sysroot] using cached artifact: $ARCHIVE"
    exit 0
fi

download_archive() {
    local out="$1"
    local tmp="$out.download"
    rm -f "$tmp"

    if [[ "$SOURCE_URL" == *drive.google.com* ]] && [ -n "$SOURCE_FILE_ID" ]; then
        local direct="https://drive.usercontent.google.com/download?id=${SOURCE_FILE_ID}&export=download&confirm=t"
        echo "[android-sysroot] downloading Google Drive artifact: $SOURCE_FILE_ID"
        curl -fL --retry 3 --connect-timeout 20 -o "$tmp" "$direct"
    else
        echo "[android-sysroot] downloading artifact: $SOURCE_URL"
        curl -fL --retry 3 --connect-timeout 20 -o "$tmp" "$SOURCE_URL"
    fi

    if file "$tmp" | grep -qiE 'HTML|text'; then
        rm -f "$tmp"
        echo "Android sysroot artifact download returned an HTML/text page." >&2
        echo "If this is Google Drive, make the file public to anyone with the link," >&2
        echo "or pre-download it to $CACHE_DIR and rerun with --archive PATH." >&2
        exit 1
    fi
    mv "$tmp" "$out"
}

if [ "$FORCE" = true ] || [ ! -s "$ARCHIVE" ]; then
    download_archive "$ARCHIVE"
else
    echo "[android-sysroot] using cached artifact: $ARCHIVE"
fi

if [ ! -f "$ARCHIVE" ]; then
    echo "Android sysroot artifact not found: $ARCHIVE" >&2
    exit 2
fi

if [ "$FORCE" = false ] &&
   "$ROOT_DIR/tools/check-android-art-sysroot.sh" --sysroot "$SYSROOT" --quiet >/dev/null 2>&1; then
    echo "[android-sysroot] ready: $SYSROOT"
    echo "[android-sysroot] using cached artifact: $ARCHIVE"
    exit 0
fi

WORK="$CACHE_DIR/extract-$$"
rm -rf "$WORK"
mkdir -p "$WORK"

echo "[android-sysroot] extracting: $ARCHIVE"
archive_kind="$(file "$ARCHIVE")"
if echo "$archive_kind" | grep -qi 'Zip archive'; then
    unzip -q "$ARCHIVE" -d "$WORK"
else
    tar -xf "$ARCHIVE" -C "$WORK"
fi

candidate_roots=("$WORK")
while IFS= read -r dir; do
    candidate_roots+=("$dir")
done < <(find "$WORK" -maxdepth 3 -type d \( -name system -o -name apex \) -print |
    sed 's#/system$##; s#/apex$##' | sort -u)

SOURCE_ROOT=""
for candidate in "${candidate_roots[@]}"; do
    if "$ROOT_DIR/tools/check-android-art-sysroot.sh" --sysroot "$candidate" --quiet >/dev/null 2>&1; then
        SOURCE_ROOT="$candidate"
        break
    fi
done

if [ -n "$SOURCE_ROOT" ]; then
    echo "[android-sysroot] installing prepared sysroot: $SYSROOT"
    rm -rf "$SYSROOT"
    mkdir -p "$SYSROOT"
    if command -v ditto >/dev/null 2>&1; then
        ditto "$SOURCE_ROOT" "$SYSROOT"
    else
        cp -Rp "$SOURCE_ROOT/." "$SYSROOT/"
    fi
else
    SOURCE_ROOT="${candidate_roots[1]:-$WORK}"
    echo "[android-sysroot] importing Android root from: $SOURCE_ROOT"
    rm -rf "$SYSROOT"
    mkdir -p "$SYSROOT"
    "$ROOT_DIR/tools/import-android-art-sysroot.sh" --from "$SOURCE_ROOT" --sysroot "$SYSROOT"
fi

ensure_linker_aliases
install_bootstrap_jar
"$ROOT_DIR/tools/check-android-art-sysroot.sh" --sysroot "$SYSROOT"

rm -rf "$WORK"
echo "[android-sysroot] ready: $SYSROOT"
