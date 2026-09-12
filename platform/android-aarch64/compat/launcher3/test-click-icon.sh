#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
FIXTURE="$ROOT_DIR/build/launcher3/fixture/Launcher3.apk"
PREFIX_DIR="$HOME/.muplar/prefixes/android-arm64"
LOG="${TMPDIR:-/tmp}/muplar-click-test.log"

pkill -f 'muplard' 2>/dev/null || true
rm -f "$PREFIX_DIR/run/muplard.sock" "$PREFIX_DIR/run/muplard.pid"

mkdir -p "$(dirname "$LOG")"
: > "$LOG"

echo "Starting Launcher3..."
export MUPLAR_SERVICE_SOCKET="$PREFIX_DIR/run/muplard.sock"
export MUPLAR_ANDROID_SOFTWARE_FRAME_PATH="/data/local/tmp/muplar/frames/software-frame.mhr"
export MUPLAR_DEBUG_VIEWS="1"
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

echo "Waiting for all apps to bind..."
for _ in {1..200}; do
    if grep -q 'bindAllApps' "$LOG"; then
        break
    fi
    sleep 0.1
done

SOCK_PATH="$PREFIX_DIR/run/muplard.sock"

echo "Sending all-apps action to open AllApps drawer..."
python3 -c "
import socket, struct, time
magic = 0x4d555044
version = 1
req_id = 1
payload = b'all-apps\nlauncher'
header = struct.pack('<IHHI4xQ', magic, version, 23, len(payload), req_id)
sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect('$SOCK_PATH')
sock.sendall(header + payload)
resp = sock.recv(1024)
sock.close()
"

sleep 1.0

echo "Sending tap at (232, 380)..."
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

send_input(0, 232.0, 380.0) # DOWN
time.sleep(0.05)
send_input(1, 232.0, 380.0) # UP
"

sleep 2.0

echo "Log output related to click/dispatch:"
tail -n 60 "$LOG"
