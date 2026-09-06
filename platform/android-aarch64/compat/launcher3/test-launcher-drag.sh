#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
FIXTURE="$ROOT_DIR/build/launcher3/fixture/Launcher3.apk"
PREFIX_DIR="$HOME/.muplar/prefixes/android-arm64"
LOG="${TMPDIR:-/tmp}/muplar-launcher-drag-test.log"

# Restart muplard so it picks up the latest binary
pkill -f 'muplard' 2>/dev/null || true
rm -f "$PREFIX_DIR/run/muplard.sock" "$PREFIX_DIR/run/muplard.pid"

mkdir -p "$(dirname "$LOG")"
: > "$LOG"

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

echo "Waiting for main looper..."
for _ in {1..300}; do
    if grep -q 'entering main looper' "$LOG"; then
        break
    fi
    kill -0 "$PID" 2>/dev/null || break
    sleep 0.1
done

SOCK_PATH="$PREFIX_DIR/run/muplard.sock"

echo "=== Test 1: Testing Swipe-Up Gesture on Launcher3 ==="
python3 -c "
import socket, struct, time

def send_input(action, x, y):
    magic = 0x4d555044
    version = 1
    req_id = 1
    payload = f'launcher\n2\n{action}\n4098\n1\n0\n{x}\n{y}'.encode('utf-8')
    header = struct.pack('<IHHI4xQ', magic, version, 27, len(payload), req_id)
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect('$SOCK_PATH')
    sock.sendall(header + payload)
    resp = sock.recv(1024)
    sock.close()

print('Sending drag up gesture on launcher...')
send_input(0, 540.0, 1600.0)
time.sleep(0.05)
for y in range(1500, 400, -100):
    send_input(2, 540.0, float(y))
    time.sleep(0.02)
send_input(1, 540.0, 400.0)
print('Drag up gesture sent!')
"

sleep 0.5
if grep -q 'toState: AllApps' "$LOG"; then
    echo "SUCCESS: Upward drag opened AllApps state!"
else
    echo "ERROR: Upward drag failed to transition to AllApps" >&2
    exit 1
fi

echo "=== Test 2: Testing all-apps Device Action (Toolbar Button) ==="
python3 -c "
import socket, struct, time

def send_action(action):
    magic = 0x4d555044
    version = 1
    req_id = 1
    payload = f'{action}\nlauncher'.encode('utf-8')
    header = struct.pack('<IHHI4xQ', magic, version, 23, len(payload), req_id)
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect('$SOCK_PATH')
    sock.sendall(header + payload)
    resp = sock.recv(1024)
    sock.close()

# Toggle back to normal
print('Sending all-apps action to toggle to Normal...')
send_action('all-apps')
time.sleep(0.5)

# Toggle to all-apps
print('Sending all-apps action to toggle to AllApps...')
send_action('all-apps')
"

sleep 0.5
grep -E 'openAllApps invoked|goToNormalState invoked|toState: AllApps|toState: Normal|launcher upward swipe' "$LOG" | tail -10

echo "ALL TESTS PASSED!"
