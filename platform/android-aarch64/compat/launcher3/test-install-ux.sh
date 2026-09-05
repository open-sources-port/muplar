#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
FIXTURE="$ROOT_DIR/build/launcher3/fixture/Launcher3.apk"
TEST_APK="$ROOT_DIR/build/android-ui-test/muplar-ui-test.apk"
PREFIX_DIR="$HOME/.muplar/prefixes/android-arm64"
LOG="${TMPDIR:-/tmp}/muplar-install-ux-test.log"

if [[ ! -f "$FIXTURE" ]]; then
    echo "Launcher3 fixture is missing; run fetch-official-apk.sh first" >&2
    exit 1
fi

if [[ ! -f "$TEST_APK" ]]; then
    echo "Building muplar-ui-test.apk..."
    "$ROOT_DIR/tests/assets/android-ui/build-apk.sh"
fi

mkdir -p "$(dirname "$LOG")"
: > "$LOG"

# Clean any existing uitest from packages dir & registry initially
rm -f "$PREFIX_DIR/packages/muplar-ui-test.apk" 2>/dev/null || true
mkdir -p "$PREFIX_DIR/packages" "$PREFIX_DIR/registry"
if [[ -f "$PREFIX_DIR/registry/android-packages.properties" ]]; then
    sed -i '' '/com.muplar.uitest/,/---/d' "$PREFIX_DIR/registry/android-packages.properties" 2>/dev/null || true
fi

echo "Starting Launcher3 without secondary app installed..."
export MUPLAR_SERVICE_SOCKET="$PREFIX_DIR/run/muplard.sock"
"$ROOT_DIR/build/bin/mup" --prefix android-arm64 --apk "$FIXTURE" \
    >"$LOG" 2>&1 &
PID=$!
cleanup() {
    set +e
    pkill -TERM -P "$PID" 2>/dev/null
    kill "$PID" 2>/dev/null
    wait "$PID" 2>/dev/null
    exit 0
}
trap cleanup EXIT

echo "Waiting for Launcher3 main looper..."
for _ in {1..300}; do
    if grep -q 'entering main looper' "$LOG"; then
        break
    fi
    kill -0 "$PID" 2>/dev/null || break
    sleep 0.1
done

if ! grep -q 'entering main looper' "$LOG"; then
    echo "Launcher3 did not enter main looper" >&2
    tail -80 "$LOG" >&2
    exit 1
fi

echo "Waiting for Launcher3 onAppsChangedListener registration..."
for _ in {1..250}; do
    if grep -q 'registered onAppsChangedListener' "$LOG"; then
        break
    fi
    sleep 0.1
done

if ! grep -q 'registered onAppsChangedListener' "$LOG"; then
    echo "Launcher3 did not register onAppsChangedListener" >&2
    tail -80 "$LOG" >&2
    exit 1
fi
echo "Launcher3 registered onAppsChangedListener successfully."

echo "Finding muplard.sock..."
SOCK_PATH=""
for _ in {1..50}; do
    for dir in "$PREFIX_DIR/run" "$HOME/.muplar" "$HOME/.local/share/muplar" "${TMPDIR:-/tmp}"; do
        if [[ -S "$dir/muplard.sock" ]]; then
            SOCK_PATH="$dir/muplard.sock"
            break 2
        fi
    done
    sleep 0.1
done

if [[ -z "$SOCK_PATH" ]]; then
    echo "Could not find muplard.sock" >&2
    tail -80 "$LOG" >&2
    exit 1
fi
echo "Found muplard.sock at: $SOCK_PATH"

echo "=== Step 1: Simulating In-Session APK Installation ==="
# Copy APK into prefix packages
cp "$TEST_APK" "$PREFIX_DIR/packages/muplar-ui-test.apk"

# Update registry text
cat <<EOF >> "$PREFIX_DIR/registry/android-packages.properties"
package=com.muplar.uitest
activity=com.muplar.uitest.MainActivity
label=UiTest
apk=$PREFIX_DIR/packages/muplar-ui-test.apk
---
EOF

echo "=== Step 2: Dispatching package-installed notification ==="
python3 "$SCRIPT_DIR/test-backstack.py" "$SOCK_PATH" package-installed "com.muplar.uitest" "$PREFIX_DIR/packages/muplar-ui-test.apk"

echo "Waiting for guest runtime to dispatch onPackageAdded to Launcher3..."
for _ in {1..100}; do
    if grep -q 'dispatched onPackageAdded to' "$LOG"; then
        break
    fi
    sleep 0.1
done

if ! grep -q 'dispatched onPackageAdded to' "$LOG"; then
    echo "onPackageAdded was not dispatched" >&2
    tail -80 "$LOG" >&2
    exit 1
fi
echo "Dynamic package addition dispatched to Launcher3 listener!"

echo "=== Step 3: Launching Newly Installed App ==="
python3 "$SCRIPT_DIR/test-backstack.py" "$SOCK_PATH" focus-tab "$PREFIX_DIR/packages/muplar-ui-test.apk"

for _ in {1..200}; do
    if grep -q 'registered activity tab=com.muplar.uitest.*MainActivity' "$LOG"; then
        break
    fi
    sleep 0.1
done

if ! grep -q 'registered activity tab=com.muplar.uitest.*MainActivity' "$LOG"; then
    echo "MainActivity was not launched from newly installed package" >&2
    tail -80 "$LOG" >&2
    exit 1
fi
echo "Newly installed app launched and resumed successfully!"

echo "=== Step 4: Back navigation to Launcher3 ==="
python3 "$SCRIPT_DIR/test-backstack.py" "$SOCK_PATH" back

for _ in {1..100}; do
    if grep -q 'activity resumed tab=launcher' "$LOG"; then
        break
    fi
    sleep 0.1
done

if ! grep -q 'activity resumed tab=launcher' "$LOG"; then
    echo "Launcher3 was not resumed" >&2
    tail -80 "$LOG" >&2
    exit 1
fi

echo "=== Log Verification ==="
grep -E 'registered onAppsChangedListener|package installed notification|notifyPackageAdded|dispatched onPackageAdded|registered activity tab=com.muplar.uitest|activity resumed tab=launcher' "$LOG"

echo "SUCCESS: In-session App Install UX and dynamic Launcher3 refresh verified end-to-end!"
