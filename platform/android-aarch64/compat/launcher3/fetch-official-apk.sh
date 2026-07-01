#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
WORK="$ROOT_DIR/build/launcher3/system-image"
ARCHIVE="$WORK/arm64-v8a-35_r02.zip"
URL="https://dl.google.com/android/repository/sys-img/android/arm64-v8a-35_r02.zip"
SHA1="2026a06409db630b56711afdbffb457c1dbaed49"
PYDEPS="$WORK/pydeps"
FIXTURE="$ROOT_DIR/build/launcher3/fixture/Launcher3.apk"
SEVENZIP="${SEVENZIP:-$(command -v 7zz 2>/dev/null || true)}"
if [ -z "$SEVENZIP" ] && [ -x /opt/homebrew/bin/7zz ]; then
    SEVENZIP=/opt/homebrew/bin/7zz
fi

if [ -f "$FIXTURE" ]; then
    echo "[Launcher3] official fixture already ready: $FIXTURE"
    exit 0
fi

mkdir -p "$WORK"
if [ -z "$SEVENZIP" ] || [ ! -x "$SEVENZIP" ]; then
    echo "7zz is required for read-only ext4 extraction." >&2
    echo "Install Homebrew sevenzip or set SEVENZIP=/path/to/7zz." >&2
    exit 1
fi
curl -L --fail --continue-at - "$URL" -o "$ARCHIVE"
printf '%s  %s\n' "$SHA1" "$ARCHIVE" | shasum -a 1 -c -
unzip -p "$ARCHIVE" arm64-v8a/system.img > "$WORK/disk.img"

# The emulator disk is GPT; partition 2 is the Android dynamic super partition.
dd if="$WORK/disk.img" of="$WORK/super.img" bs=512 \
    skip=4096 count=3162112 status=none
python3 -m pip install --disable-pip-version-check --quiet \
    --target "$PYDEPS" unsuper numpy
PYTHONPATH="$PYDEPS" python3 "$PYDEPS/unsuper.py" \
    "$WORK/super.img" "$WORK/partitions" -p system_ext -j 1 -q

rm -rf "$WORK/extracted"
"$SEVENZIP" x -y -o"$WORK/extracted" \
    "$WORK/partitions/system_ext.img" \
    'priv-app/Launcher3QuickStep/Launcher3QuickStep.apk' >/dev/null
cp "$WORK/extracted/priv-app/Launcher3QuickStep/Launcher3QuickStep.apk" \
    "$WORK/Launcher3QuickStep.apk"

LAUNCHER3_ARTIFACT_PROVIDER=official-system-image \
LAUNCHER3_ARTIFACT_REF='system-images;android-35;default;arm64-v8a_r02' \
LAUNCHER3_ARTIFACT_CHECKSUM="$SHA1" \
    "$SCRIPT_DIR/import-apk.sh" "$WORK/Launcher3QuickStep.apk"

rm -rf "$ARCHIVE" "$WORK/disk.img" "$WORK/super.img" "$WORK/partitions" \
    "$WORK/pydeps" "$WORK/Launcher3QuickStep.apk"
rm -rf "$WORK/extracted"
echo "[Launcher3] official APK fixture imported"
