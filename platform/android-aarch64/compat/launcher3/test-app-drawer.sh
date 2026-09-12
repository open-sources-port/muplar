#!/bin/zsh
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
FIXTURE="$ROOT_DIR/build/launcher3/fixture/Launcher3.apk"
PREFIX_DIR="$HOME/.muplar/prefixes/android-arm64"
LOG="${TMPDIR:-/tmp}/muplar-app-drawer-test.log"

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

echo "Waiting for Launcher3 main looper..."
for _ in {1..300}; do
    if grep -q 'entering main looper' "$LOG"; then
        break
    fi
    kill -0 "$PID" 2>/dev/null || break
    sleep 0.1
done

echo "Waiting for initial frame..."
for _ in {1..200}; do
    if grep -q 'software frame presented' "$LOG"; then
        break
    fi
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

# Dump frame to PNG
FRAME_PATH="$HOME/.muplar/sysroots/android-arm64/api-35/sysroot/data/local/tmp/muplar/frames/software-frame.mhr"
python3 -c "
import os, struct, subprocess

path = '$FRAME_PATH'
if not os.path.exists(path):
    print('Frame not found at', path)
    exit(1)

with open(path, 'rb') as f:
    header = f.read(24)
    magic, w, h, stride, nbytes = struct.unpack('<IIIIQ', header)
    pixels = f.read(nbytes)

file_size = 54 + w * h * 4
bmp_header = b'BM' + struct.pack('<IHHI', file_size, 0, 0, 54) + struct.pack('<IiiHHIIIIII', 40, w, -h, 1, 32, 0, w * h * 4, 2835, 2835, 0, 0)
bgra = bytearray(len(pixels))
for i in range(0, len(pixels), 4):
    bgra[i] = pixels[i+2]
    bgra[i+1] = pixels[i+1]
    bgra[i+2] = pixels[i]
    bgra[i+3] = pixels[i+3]

out_bmp = '/Users/dbaotrung/.gemini/antigravity-ide/brain/d1c1174a-4369-4e8a-a587-c75c3ebc3deb/all-apps-frame.bmp'
out_png = '/Users/dbaotrung/.gemini/antigravity-ide/brain/d1c1174a-4369-4e8a-a587-c75c3ebc3deb/all-apps-frame.png'
with open(out_bmp, 'wb') as f:
    f.write(bmp_header + bgra)
subprocess.run(['sips', '-s', 'format', 'png', out_bmp, '--out', out_png], check=True)
print('AllApps frame saved to:', out_png)
"

grep -E 'openAllApps|toState: AllApps|goToNormalState' "$LOG" | tail -10
