#!/bin/zsh
# Test muplard clipboard protocol and synchronization
#
# Copyright 2026 muplar contributors
# SPDX-License-Identifier: Apache-2.0

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/../../../.." && pwd)"
MUPLARD_BIN="$ROOT_DIR/build/bin/muplard"
SOCK_PATH="${TMPDIR:-/tmp}/muplar-clipboard-test-$$.sock"

if [[ ! -x "$MUPLARD_BIN" ]]; then
    echo "muplard binary not found at $MUPLARD_BIN" >&2
    exit 1
fi

rm -f "$SOCK_PATH"
"$MUPLARD_BIN" --socket "$SOCK_PATH" &
PID=$!

cleanup() {
    kill "$PID" 2>/dev/null || true
    wait "$PID" 2>/dev/null || true
    rm -f "$SOCK_PATH"
}
trap cleanup EXIT INT TERM

# Wait for socket to become ready
for _ in {1..50}; do
    if [[ -S "$SOCK_PATH" ]]; then
        break
    fi
    sleep 0.05
done

python3 -c "
import socket, struct, sys

sock_path = '$SOCK_PATH'

def send_req(opcode, payload_str):
    magic = 0x4d555044
    version = 1
    req_id = 100
    payload = payload_str.encode('utf-8')
    header = struct.pack('<IHHI4xQ', magic, version, opcode, len(payload), req_id)
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(sock_path)
    s.sendall(header + payload)
    resp_hdr = s.recv(24)
    r_magic, r_ver, r_op, r_size, r_id = struct.unpack('<IHHI4xQ', resp_hdr)
    r_payload = s.recv(r_size).decode('utf-8')
    s.close()
    return r_op, r_payload

# 1. Verify initial clipboard is empty
op, text = send_req(32, '')
assert op == (32 | 0x8000), f'Invalid reply opcode {op}'
assert text == '', f'Expected empty initial clipboard, got {text}'

# 2. Set clipboard
op, gen = send_req(33, 'Hello from Host Pasteboard!')
assert op == (33 | 0x8000)
assert gen == '1', f'Expected generation 1, got {gen}'

# 3. Retrieve and verify clipboard text
op, text = send_req(32, '')
assert text == 'Hello from Host Pasteboard!', f'Mismatch: {text}'

# 4. Update clipboard text
op, gen = send_req(33, 'Muplar Clipboard Sync Verified')
assert gen == '2', f'Expected generation 2, got {gen}'

op, text = send_req(32, '')
assert text == 'Muplar Clipboard Sync Verified', f'Mismatch: {text}'

print('[ClipboardTest] Verified SetClipboard (33) and GetClipboard (32) protocol handling')
"

echo "SUCCESS: Clipboard synchronization protocol verified successfully!"

