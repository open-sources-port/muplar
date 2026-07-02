#!/bin/bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
source "$SCRIPT_DIR/PIN.env"

AOSP_ROOT="${1:-}"
LUNCH_TARGET="${2:-aosp_arm64-trunk_staging-userdebug}"
if [[ -z "$AOSP_ROOT" ]]; then
    echo "Usage: $0 AOSP_ROOT [LUNCH_TARGET]" >&2
    exit 2
fi
AOSP_ROOT="$(cd "$AOSP_ROOT" && pwd)"
LAUNCHER_SOURCE="$AOSP_ROOT/packages/apps/Launcher3"

if [[ ! -f "$AOSP_ROOT/build/envsetup.sh" || ! -d "$LAUNCHER_SOURCE/.git" ]]; then
    echo "Not an AOSP checkout with packages/apps/Launcher3: $AOSP_ROOT" >&2
    exit 1
fi
ACTUAL="$(git -C "$LAUNCHER_SOURCE" rev-parse HEAD)"
if [[ "$ACTUAL" != "$LAUNCHER3_COMMIT" ]]; then
    echo "Launcher3 revision mismatch: expected $LAUNCHER3_COMMIT, got $ACTUAL" >&2
    exit 1
fi

cd "$AOSP_ROOT"
source build/envsetup.sh >/dev/null
lunch "$LUNCH_TARGET" >/dev/null
m "$LAUNCHER3_MODULE"

APK="$(find "$OUT" -type f \( -path '*/Launcher3/Launcher3.apk' -o \
    -path '*/Launcher3QuickStep/Launcher3QuickStep.apk' \) -print | head -1)"
if [[ -z "$APK" ]]; then
    echo "Soong completed but no Launcher3 APK was found under $OUT" >&2
    exit 1
fi
"$SCRIPT_DIR/import-apk.sh" "$APK"
echo "[Launcher3] pinned AOSP build imported from $APK"
