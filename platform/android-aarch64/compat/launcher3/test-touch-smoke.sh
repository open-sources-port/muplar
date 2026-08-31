#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
FIXTURE="$ROOT_DIR/build/launcher3/fixture/Launcher3.apk"
LOG="${TMPDIR:-/tmp}/muplar-launcher3-smoke.log"

if [[ ! -f "$FIXTURE" ]]; then
    echo "Launcher3 fixture is missing; run fetch-official-apk.sh first" >&2
    exit 1
fi

mkdir -p "$(dirname "$LOG")"
: > "$LOG"

"$ROOT_DIR/build/bin/mup" --prefix android-arm64 --apk "$FIXTURE" \
    >"$LOG" 2>&1 &
PID=$!
cleanup() {
    pkill -TERM -P "$PID" 2>/dev/null || true
    kill "$PID" 2>/dev/null || true
    wait "$PID" 2>/dev/null || true
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
    candidate="$(find "$ROOT_DIR/build" "${TMPDIR:-/tmp}" "$HOME/.local/share/muplar" -name "muplard.sock" 2>/dev/null | head -n 1 || true)"
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
python3 "$SCRIPT_DIR/test-touch-dispatch.py" "$SOCK_PATH"

sleep 0.5
echo "--- Log Output After Touch Dispatch ---"
grep -iE 'motion trace|dispatch|MotionEvent|SIGSEGV|exit code|fault|crash' "$LOG" || tail -40 "$LOG"
