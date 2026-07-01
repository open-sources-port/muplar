#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
VERSION="2.4"
ARCHIVE="dex-tools-v${VERSION}.zip"
URL="https://github.com/pxb1988/dex2jar/releases/download/v${VERSION}/${ARCHIVE}"
DESTINATION="$ROOT_DIR/third_party/dex-tools-bin"
CACHE="$ROOT_DIR/build/downloads/$ARCHIVE"

if [ -x "$DESTINATION/d2j-dex2jar.sh" ]; then
    echo "[dex2jar] already installed: $DESTINATION"
    exit 0
fi

mkdir -p "$(dirname "$CACHE")" "$DESTINATION"
curl -L --fail --continue-at - "$URL" -o "$CACHE"
TEMP="$ROOT_DIR/build/dex-tools-install-$$"
rm -rf "$TEMP"
mkdir -p "$TEMP"
unzip -q "$CACHE" -d "$TEMP"
SOURCE="$(find "$TEMP" -type f -name d2j-dex2jar.sh -print | head -1)"
if [ -z "$SOURCE" ]; then
    echo "dex2jar archive does not contain d2j-dex2jar.sh" >&2
    exit 1
fi
cp -R "$(dirname "$SOURCE")"/. "$DESTINATION"/
chmod +x "$DESTINATION"/*.sh
rm -rf "$TEMP"
echo "[dex2jar] installed: $DESTINATION"
