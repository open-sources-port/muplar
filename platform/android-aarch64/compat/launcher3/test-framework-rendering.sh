#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
FIXTURE="$ROOT_DIR/build/launcher3/fixture/Launcher3.apk"
TEST_APK="$ROOT_DIR/build/android-ui-test/muplar-ui-test.apk"
PREFIX_DIR="$HOME/.muplar/prefixes/android-arm64"
LOG="${TMPDIR:-/tmp}/muplar-framework-rendering-test.log"

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

# Ensure test apk is in packages dir & registry
mkdir -p "$PREFIX_DIR/packages" "$PREFIX_DIR/registry"
cp -f "$TEST_APK" "$PREFIX_DIR/packages/muplar-ui-test.apk"

if ! grep -q "com.muplar.uitest" "$PREFIX_DIR/registry/android-packages.properties" 2>/dev/null; then
    cat <<EOF >> "$PREFIX_DIR/registry/android-packages.properties"
package=com.muplar.uitest
activity=com.muplar.uitest.MainActivity
label=UiTest
apk=$PREFIX_DIR/packages/muplar-ui-test.apk
---
EOF
fi

echo "Starting Launcher3..."
export MUPLAR_SERVICE_SOCKET="$PREFIX_DIR/run/muplard.sock"
export MUPLAR_ANDROID_SOFTWARE_FRAME_PATH="/data/local/tmp/muplar/frames/software-frame.mhr"
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

echo "Waiting for event-driven frame presenter initialization..."
for _ in {1..200}; do
    if grep -q 'frame presenter event-driven loop active' "$LOG"; then
        break
    fi
    sleep 0.1
done

if ! grep -q 'frame presenter event-driven loop active' "$LOG"; then
    echo "Event-driven frame presenter was not initialized" >&2
    tail -80 "$LOG" >&2
    exit 1
fi
echo "Event-driven frame presenter loop is active."

echo "Waiting for initial frame presentation..."
for _ in {1..100}; do
    if grep -q 'software frame presented' "$LOG"; then
        break
    fi
    sleep 0.1
done

if ! grep -q 'software frame presented' "$LOG"; then
    echo "No frame was presented" >&2
    tail -80 "$LOG" >&2
    exit 1
fi
echo "Initial frame presented successfully."

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

echo "=== Step 1: Testing Event-Driven Frame Presentation on Touch ==="
BEFORE_COUNT=$(grep -c 'software frame presented' "$LOG" || true)
python3 "$SCRIPT_DIR/test-touch-dispatch.py" "$SOCK_PATH"

for _ in {1..50}; do
    AFTER_COUNT=$(grep -c 'software frame presented' "$LOG" || true)
    if [[ "$AFTER_COUNT" -gt "$BEFORE_COUNT" ]]; then
        break
    fi
    sleep 0.05
done

if [[ "$AFTER_COUNT" -le "$BEFORE_COUNT" ]]; then
    echo "Touch event did not trigger immediate frame presentation" >&2
    tail -80 "$LOG" >&2
    exit 1
fi
echo "Touch event triggered immediate frame presentation (before=$BEFORE_COUNT, after=$AFTER_COUNT)!"

echo "=== Step 2: Testing App Launch and Frame Presentation ==="
python3 "$SCRIPT_DIR/test-backstack.py" "$SOCK_PATH" focus-tab "$PREFIX_DIR/packages/muplar-ui-test.apk"

for _ in {1..200}; do
    if grep -q 'registered activity tab=com.muplar.uitest.*MainActivity' "$LOG"; then
        break
    fi
    sleep 0.1
done

if ! grep -q 'registered activity tab=com.muplar.uitest.*MainActivity' "$LOG"; then
    echo "MainActivity was not launched" >&2
    tail -80 "$LOG" >&2
    exit 1
fi
echo "MainActivity launched and presented!"

echo "=== Step 3: Back Navigation and Restoration ==="
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
echo "Launcher3 resumed successfully!"

echo "=== Log Verification ==="
grep -E 'frame presenter event-driven loop active|software frame presented|VRI.*setView|registered activity tab=com.muplar.uitest|activity resumed tab=launcher' "$LOG" | tail -15

echo "SUCCESS: Framework-backed rendering and event-driven presentation verified end-to-end!"
