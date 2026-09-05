#!/usr/bin/env python3
import socket
import struct
import sys
import time
import os

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
    if len(sys.argv) < 2:
        print("Usage: test-touch-dispatch.py <muplard.sock_path>")
        sys.exit(1)
    sock_path = sys.argv[1]
    
    # Opcode 27 = DeviceInput
    # payload format: tab \n type \n action \n source \n device_id \n key_code \n x \n y
    # ACTION_DOWN = 0
    payload_down = "\n0\n0\n4098\n0\n0\n540.0\n960.0"
    print(f"Sending ACTION_DOWN to {sock_path}...")
    res_down = send_request(sock_path, 27, payload_down)
    print(f"ACTION_DOWN response generation: {res_down}")
    
    time.sleep(0.05)
    
    # ACTION_UP = 1
    payload_up = "\n0\n1\n4098\n0\n0\n540.0\n960.0"
    print(f"Sending ACTION_UP to {sock_path}...")
    res_up = send_request(sock_path, 27, payload_up)
    print(f"ACTION_UP response generation: {res_up}")
