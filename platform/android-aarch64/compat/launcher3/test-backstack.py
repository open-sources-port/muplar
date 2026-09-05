#!/usr/bin/env python3
import socket
import struct
import sys
import time

def send_request(sock_path, opcode, payload):
    magic = 0x4d555044
    version = 1
    req_id = 1
    payload_bytes = payload.encode('utf-8')
    header = struct.pack('<IHHI4xQ', magic, version, opcode, len(payload_bytes), req_id)
    
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect(sock_path)
    sock.sendall(header + payload_bytes)
    
    resp_header_data = sock.recv(24)
    if len(resp_header_data) < 24:
        print("Failed to receive response header", file=sys.stderr)
        sock.close()
        return None
    r_magic, r_version, r_opcode, r_size, r_id = struct.unpack('<IHHI4xQ', resp_header_data)
    resp_payload = b''
    while len(resp_payload) < r_size:
        chunk = sock.recv(r_size - len(resp_payload))
        if not chunk:
            break
        resp_payload += chunk
    sock.close()
    return resp_payload.decode('utf-8', errors='replace')

def send_key(sock_path, tab, key_code):
    # Opcode 27 = DeviceInput
    # format: tab \n type \n action \n source \n device_id \n key_code \n x \n y
    # type=1 (key), source=257 (keyboard), action 0=down, 1=up
    down_payload = f"{tab}\n1\n0\n257\n0\n{key_code}\n0.0\n0.0"
    send_request(sock_path, 27, down_payload)
    time.sleep(0.05)
    up_payload = f"{tab}\n1\n1\n257\n0\n{key_code}\n0.0\n0.0"
    send_request(sock_path, 27, up_payload)

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: test-backstack.py <muplard.sock_path> <action> [apk_path]")
        sys.exit(1)
    sock_path = sys.argv[1]
    action = sys.argv[2]
    
    if action in ("focus-tab", "launch"):
        apk_path = sys.argv[3] if len(sys.argv) > 3 else ""
        payload = f"focus-tab\ncom.muplar.uitest\n{apk_path}\ncom.muplar.uitest\ncom.muplar.uitest.MainActivity\ncom.muplar.uitest.UiTestApplication"
        print(f"Sending focus-tab for com.muplar.uitest to {sock_path}...")
        res = send_request(sock_path, 23, payload)
        print(f"focus-tab generation response: {res}")
    elif action == "launch-second":
        # Key L = 40 in Android KeyEvent
        print(f"Sending KEYCODE_L (40) to trigger startActivity(SecondActivity)...")
        send_key(sock_path, "com.muplar.uitest", 40)
    elif action == "finish-second":
        # Key F = 34 in Android KeyEvent
        print(f"Sending KEYCODE_F (34) to trigger finish() in SecondActivity...")
        send_key(sock_path, "com.muplar.uitest", 34)
    elif action == "package-installed":
        pkg = sys.argv[3] if len(sys.argv) > 3 else "com.muplar.uitest"
        apk_path = sys.argv[4] if len(sys.argv) > 4 else ""
        payload = f"package-installed\n{pkg}\n{apk_path}\n{pkg}\n\n"
        print(f"Sending package-installed for {pkg} to {sock_path}...")
        res = send_request(sock_path, 23, payload)
        print(f"package-installed generation response: {res}")
    elif action == "back":
        payload = "back\ncom.muplar.uitest"
        print(f"Sending back action to {sock_path}...")
        res = send_request(sock_path, 23, payload)
        print(f"back generation response: {res}")
    else:
        print(f"Unknown action: {action}", file=sys.stderr)
        sys.exit(1)
