#!/usr/bin/env python3
import socket
import struct
import sys

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

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("Usage: test-app-launch.py <muplard.sock_path> <action> [apk_path]")
        sys.exit(1)
    sock_path = sys.argv[1]
    action = sys.argv[2]
    
    # Opcode 23 = DeviceAction
    # format: action \n tab \n apk \n package_name \n activity \n application
    if action in ("focus-tab", "launch"):
        apk_path = sys.argv[3] if len(sys.argv) > 3 else ""
        payload = f"focus-tab\ncom.muplar.uitest\n{apk_path}\ncom.muplar.uitest\ncom.muplar.uitest.MainActivity\ncom.muplar.uitest.UiTestApplication"
        print(f"Sending focus-tab for com.muplar.uitest to {sock_path}...")
        res = send_request(sock_path, 23, payload)
        print(f"focus-tab generation response: {res}")
    elif action == "back":
        payload = "back\ncom.muplar.uitest"
        print(f"Sending back action to {sock_path}...")
        res = send_request(sock_path, 23, payload)
        print(f"back generation response: {res}")
    elif action == "home":
        payload = "home\nlauncher"
        print(f"Sending home action to {sock_path}...")
        res = send_request(sock_path, 23, payload)
        print(f"home generation response: {res}")
    elif action == "recents":
        payload = "recents\nlauncher"
        print(f"Sending recents action to {sock_path}...")
        res = send_request(sock_path, 23, payload)
        print(f"recents generation response: {res}")
    else:
        print(f"Unknown action: {action}", file=sys.stderr)
        sys.exit(1)
