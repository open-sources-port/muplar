#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
FIXTURE="$ROOT_DIR/build/launcher3/fixture/Launcher3.apk"
TEST_APK="$ROOT_DIR/build/android-ui-test/muplar-ui-test.apk"
LOG="${TMPDIR:-/tmp}/muplar-backstack-test.log"

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
    candidate="$(find "$HOME/.muplar" "$HOME/.local/share/muplar" "${TMPDIR:-/tmp}" "$ROOT_DIR/build" -name "muplard.sock" 2>/dev/null | head -n 1 || true)"
    if [[ -n "$candidate" && -S "$candidate" ]]; then
        SOCK_PATH="$candidate"
        break
    fi
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

echo "=== Step 1: Launching secondary app (MainActivity) ==="
python3 "$SCRIPT_DIR/test-backstack.py" "$SOCK_PATH" focus-tab "$TEST_APK"

for _ in {1..300}; do
    if grep -q 'registered activity tab=com.muplar.uitest.*MainActivity' "$LOG"; then
        break
    fi
    sleep 0.1
done

if ! grep -q 'registered activity tab=com.muplar.uitest.*MainActivity' "$LOG"; then
    echo "MainActivity was not registered" >&2
    tail -80 "$LOG" >&2
    exit 1
fi
echo "MainActivity launched successfully."

echo "=== Step 2: Triggering in-app startActivity(SecondActivity) ==="
python3 "$SCRIPT_DIR/test-backstack.py" "$SOCK_PATH" launch-second

for _ in {1..100}; do
    if grep -q 'launched activity class=com.muplar.uitest.SecondActivity' "$LOG"; then
        break
    fi
    sleep 0.1
done

if ! grep -q 'launched activity class=com.muplar.uitest.SecondActivity' "$LOG"; then
    echo "SecondActivity was not launched" >&2
    tail -80 "$LOG" >&2
    exit 1
fi
echo "SecondActivity launched and pushed onto task stack."

# Verify task size is 2
if ! grep -q 'TaskRecord.*com.muplar.uitest, size=2' "$LOG"; then
    echo "TaskRecord did not grow to size=2" >&2
    tail -80 "$LOG" >&2
    exit 1
fi
echo "Task stack verified: size=2 (MainActivity + SecondActivity)."

echo "=== Step 3: Dispatching Back navigation (pop SecondActivity) ==="
python3 "$SCRIPT_DIR/test-backstack.py" "$SOCK_PATH" back

for _ in {1..100}; do
    if grep -q 'resuming top=com.muplar.uitest.MainActivity' "$LOG"; then
        break
    fi
    sleep 0.1
done

if ! grep -q 'resuming top=com.muplar.uitest.MainActivity' "$LOG"; then
    echo "MainActivity was not resumed after popping SecondActivity" >&2
    tail -80 "$LOG" >&2
    exit 1
fi
echo "SecondActivity popped, MainActivity resumed. Tab remained active."

# Verify tab-finished was NOT called
if grep -q 'requesting tab-finished for com.muplar.uitest' "$LOG"; then
    echo "tab-finished was prematurely called while MainActivity was still in task!" >&2
    tail -80 "$LOG" >&2
    exit 1
fi

echo "=== Step 4: Dispatching Back navigation again (finish task) ==="
python3 "$SCRIPT_DIR/test-backstack.py" "$SOCK_PATH" back

for _ in {1..100}; do
    if grep -q 'activity resumed tab=launcher' "$LOG"; then
        break
    fi
    sleep 0.1
done

if ! grep -q 'activity resumed tab=launcher' "$LOG"; then
    echo "Launcher3 was not resumed after finishing task" >&2
    tail -80 "$LOG" >&2
    exit 1
fi

if ! grep -q 'requesting tab-finished for com.muplar.uitest' "$LOG"; then
    echo "tab-finished was not requested after task emptied" >&2
    tail -80 "$LOG" >&2
    exit 1
fi

echo "=== Log Verification ==="
grep -E 'TaskRecord|registered activity|startActivityInCurrentTask|launched activity|onRestart|resuming top|task empty|tab-finished|activity resumed tab=launcher' "$LOG"

echo "SUCCESS: Multi-activity task back-stack navigation verified end-to-end!"
