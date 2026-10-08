#!/usr/bin/env python3
"""
Capture actual real bytes transmitted between client and server for BHTTP/1.
Saves raw binary and formatted hexdump files.
"""

import socket
import subprocess
import time
import struct
import os

SERVER_PORT = 9876

def capture():
    # Start server
    proc = subprocess.Popen(["./serve", "./www", str(SERVER_PORT)], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    time.sleep(0.3)

    try:
        # Run bcurl to fetch /hello.txt with -v and capture
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect(("127.0.0.1", SERVER_PORT))

        # Build real request for /hello.txt
        path = "/hello.txt"
        path_bytes = path.encode('utf-8')

        # Headers: Host=localhost:9876, User-Agent=bcurl/1.0 (BHTTP/1), Accept=*/*, Connection=close
        headers = [
            (5, f"localhost:{SERVER_PORT}"), # Host
            (7, "bcurl/1.0 (BHTTP/1)"),        # User-Agent
            (6, "*/*"),                        # Accept
            (3, "close")                       # Connection
        ]

        payload = bytearray()
        payload += struct.pack("!H", len(path_bytes))
        payload += path_bytes
        payload.append(len(headers))

        for hid, val in headers:
            vbytes = val.encode('utf-8')
            payload.append(hid)
            payload += struct.pack("!H", len(vbytes))
            payload += vbytes

        # Header: Magic=0x4248, Ver=1, Type=1(REQ), Flags=1(END_STREAM), Reserved=0,0,0, PayloadLen
        req_hdr = struct.pack("!HBBBBBB I", 0x4248, 0x01, 0x01, 0x01, 0, 0, 0, len(payload))
        full_req = bytes(req_hdr + payload)

        s.sendall(full_req)

        # Read Response Header (12 bytes)
        resp_hdr_bytes = s.recv(12)
        magic, ver, ftype, flags, r0, r1, r2, plen = struct.unpack("!HBBBBBB I", resp_hdr_bytes)

        # Read Response Payload
        resp_payload_bytes = bytearray()
        while len(resp_payload_bytes) < plen:
            chunk = s.recv(plen - len(resp_payload_bytes))
            if not chunk:
                break
            resp_payload_bytes.extend(chunk)
        resp_payload_bytes = bytes(resp_payload_bytes)
        full_resp = resp_hdr_bytes + resp_payload_bytes
        s.close()

        os.makedirs("examples", exist_ok=True)

        with open("examples/request.bin", "wb") as f:
            f.write(full_req)
        with open("examples/response.bin", "wb") as f:
            f.write(full_resp)

        print(f"Captured Request: {len(full_req)} bytes")
        print(f"Captured Response: {len(full_resp)} bytes")

        def format_hexdump(data):
            lines = []
            for i in range(0, len(data), 16):
                chunk = data[i:i+16]
                hex_part = " ".join(f"{b:02x}" for b in chunk)
                if len(chunk) < 16:
                    hex_part = hex_part.ljust(48)
                else:
                    if len(chunk) > 8:
                        hex_part = " ".join(f"{b:02x}" for b in chunk[:8]) + "  " + " ".join(f"{b:02x}" for b in chunk[8:])
                ascii_part = "".join(chr(b) if 32 <= b < 127 else "." for b in chunk)
                lines.append(f"{i:04x}  {hex_part:<49} |{ascii_part}|")
            return "\n".join(lines)

        req_hex = format_hexdump(full_req)
        resp_hex = format_hexdump(full_resp)

        with open("examples/request_response.hex", "w") as f:
            f.write("=== REQUEST FRAME ===\n")
            f.write(req_hex + "\n\n")
            f.write("=== RESPONSE FRAME ===\n")
            f.write(resp_hex + "\n")

        print("Saved raw dumps to examples/request_response.hex")

    finally:
        proc.terminate()

if __name__ == "__main__":
    capture()
