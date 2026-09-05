#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
FIXTURE="$ROOT_DIR/build/launcher3/fixture/Launcher3.apk"
TEST_APK="$ROOT_DIR/build/android-ui-test/muplar-ui-test.apk"
LOG="${TMPDIR:-/tmp}/muplar-launcher3-app-launch.log"

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

echo "Starting Launcher3..."
export MUPLAR_SERVICE_SOCKET="$HOME/.muplar/prefixes/android-arm64/run/muplard.sock"
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
    if grep -qE 'bootstrap failed|failed to invoke onCreate' "$LOG"; then
        tail -80 "$LOG" >&2
        exit 1
    fi
    kill -0 "$PID" 2>/dev/null || break
    sleep 0.1
done

if ! grep -q 'entering main looper' "$LOG"; then
    echo "Launcher3 did not enter main looper" >&2
    tail -80 "$LOG" >&2
    exit 1
fi

echo "Launcher3 is running. Finding muplard.sock..."
SOCK_PATH=""
for _ in {1..50}; do
    for dir in "$HOME/.muplar/prefixes/android-arm64/run" "$HOME/.muplar" "$HOME/.local/share/muplar" "${TMPDIR:-/tmp}"; do
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
# Allow Launcher initial tasks to settle before launching secondary app
sleep 2.0

echo "Dispatching focus-tab for secondary app..."
python3 "$SCRIPT_DIR/test-app-launch.py" "$SOCK_PATH" focus-tab "$TEST_APK"

echo "Waiting for secondary app to launch and resume..."
LAUNCH_OK=false
for i in {1..900}; do
    if grep -q 'launched activity class=com.muplar.uitest.MainActivity' "$LOG" &&
       grep -q '\[UiTest\] onResume' "$LOG"; then
        LAUNCH_OK=true
        break
    fi
    if (( i % 50 == 0 )); then
        echo "Waiting for app launch... ($(( i / 10 ))s elapsed)"
    fi
    sleep 0.1
done

if [[ "$LAUNCH_OK" != true ]]; then
    echo "FAILURE: Secondary app did not resume" >&2
    grep -E 'DeviceController|Muplar/ART|UiTest' "$LOG" | tail -30 || true
    exit 1
fi
echo "Secondary app launched and resumed successfully."

sleep 0.5
echo "Dispatching back navigation..."
python3 "$SCRIPT_DIR/test-app-launch.py" "$SOCK_PATH" back

echo "Waiting for back navigation to resume Launcher3..."
BACK_OK=false
for i in {1..300}; do
    if grep -q 'activity resumed tab=launcher' "$LOG"; then
        BACK_OK=true
        break
    fi
    if (( i % 50 == 0 )); then
        echo "Waiting for back navigation... ($(( i / 10 ))s elapsed)"
    fi
    sleep 0.1
done

echo "--- Relevant Log Lines ---"
grep -E 'DeviceController|Muplar/ART|UiTest' "$LOG" | tail -30 || true

if [[ "$BACK_OK" = true ]]; then
    echo "SUCCESS: Secondary app launched, resumed, and navigated back to Launcher3 cleanly."
    exit 0
else
    echo "FAILURE: Expected back navigation to Launcher3 not observed." >&2
    exit 1
fi
