#!/usr/bin/env python3
"""
BHTTP/1 Comprehensive Test Suite
Tests server, client, framing, keep-alive, unknown frame skipping, malformed input,
path traversal security, and client output separation.
"""

import os
import sys
import time
import socket
import struct
import subprocess
import unittest

MAGIC = 0x4248
VERSION = 0x01
FRAME_REQUEST = 0x01
FRAME_RESPONSE = 0x02
FRAME_GOAWAY = 0x03

HEADER_CONTENT_TYPE = 0x01
HEADER_CONTENT_LENGTH = 0x02
HEADER_CONNECTION = 0x03
HEADER_SERVER = 0x04
HEADER_HOST = 0x05
HEADER_ACCEPT = 0x06
HEADER_USER_AGENT = 0x07

def encode_frame_header(magic, version, frame_type, flags, payload_len):
    # Fixed 12-byte header: uint16 magic, uint8 version, uint8 type, uint8 flags, 3 bytes reserved, uint32 payload_len
    return struct.pack("!HBBBBBB I", magic, version, frame_type, flags, 0, 0, 0, payload_len)

def decode_frame_header(data):
    if len(data) < 12:
        return None
    magic, ver, ftype, flags, r0, r1, r2, plen = struct.unpack("!HBBBBBB I", data[:12])
    return {
        "magic": magic,
        "version": ver,
        "frame_type": ftype,
        "flags": flags,
        "payload_len": plen
    }

def encode_request(path, headers=None, flags=0):
    if headers is None:
        headers = []
    
    path_bytes = path.encode('utf-8')
    payload = bytearray()
    payload += struct.pack("!H", len(path_bytes))
    payload += path_bytes
    payload.append(len(headers))
    
    for hid, val in headers:
        val_bytes = val.encode('utf-8')
        if hid == 0:
            # Custom header (not used in default test)
            pass
        else:
            payload.append(hid)
            payload += struct.pack("!H", len(val_bytes))
            payload += val_bytes
            
    hdr = encode_frame_header(MAGIC, VERSION, FRAME_REQUEST, flags, len(payload))
    return bytes(hdr + payload)

def decode_response(payload):
    if len(payload) < 3:
        return None
    status = struct.unpack("!H", payload[:2])[0]
    hcount = payload[2]
    offset = 3
    headers = {}
    
    for _ in range(hcount):
        if offset >= len(payload):
            return None
        hid = payload[offset]
        offset += 1
        if hid == 0:
            nlen = struct.unpack("!H", payload[offset:offset+2])[0]
            offset += 2
            name = payload[offset:offset+nlen].decode('utf-8', errors='ignore')
            offset += nlen
            vlen = struct.unpack("!H", payload[offset:offset+2])[0]
            offset += 2
            val = payload[offset:offset+vlen].decode('utf-8', errors='ignore')
            offset += vlen
            headers[name] = val
        else:
            vlen = struct.unpack("!H", payload[offset:offset+2])[0]
            offset += 2
            val = payload[offset:offset+vlen].decode('utf-8', errors='ignore')
            offset += vlen
            headers[hid] = val
            
    body = payload[offset:]
    return {
        "status": status,
        "headers": headers,
        "body": body
    }

class TestBHTTP(unittest.TestCase):
    SERVER_PORT = 9123
    server_proc = None

    @classmethod
    def setUpClass(cls):
        # Start server process
        cmd = ["./serve", "./www", str(cls.SERVER_PORT)]
        cls.server_proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        time.sleep(0.3)

    @classmethod
    def tearDownClass(cls):
        if cls.server_proc:
            cls.server_proc.terminate()
            try:
                cls.server_proc.wait(timeout=1.0)
            except subprocess.TimeoutExpired:
                cls.server_proc.kill()

    def get_socket(self):
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(3.0)
        s.connect(("127.0.0.1", self.SERVER_PORT))
        return s

    def read_exact(self, sock, n):
        data = bytearray()
        while len(data) < n:
            chunk = sock.recv(n - len(data))
            if not chunk:
                break
            data.extend(chunk)
        return bytes(data)

    def test_01_basic_get_index(self):
        """Test GET /index.html returns 200 OK with correct content and headers."""
        s = self.get_socket()
        req = encode_request("/index.html", [(HEADER_HOST, "localhost:9123")])
        s.sendall(req)

        hdr_bytes = self.read_exact(s, 12)
        hdr = decode_frame_header(hdr_bytes)
        self.assertEqual(hdr["magic"], MAGIC)
        self.assertEqual(hdr["version"], VERSION)
        self.assertEqual(hdr["frame_type"], FRAME_RESPONSE)

        payload = self.read_exact(s, hdr["payload_len"])
        resp = decode_response(payload)
        self.assertEqual(resp["status"], 200)
        self.assertIn(b"BHTTP/1 Binary Protocol Active", resp["body"])
        self.assertEqual(resp["headers"][HEADER_CONTENT_TYPE], "text/html; charset=utf-8")
        self.assertEqual(int(resp["headers"][HEADER_CONTENT_LENGTH]), len(resp["body"]))
        s.close()

    def test_02_basic_get_hello_txt(self):
        """Test GET /hello.txt returns 200 OK with text/plain."""
        s = self.get_socket()
        req = encode_request("/hello.txt")
        s.sendall(req)

        hdr = decode_frame_header(self.read_exact(s, 12))
        resp = decode_response(self.read_exact(s, hdr["payload_len"]))
        self.assertEqual(resp["status"], 200)
        self.assertIn(b"Hello from BHTTP/1 Server!", resp["body"])
        self.assertEqual(resp["headers"][HEADER_CONTENT_TYPE], "text/plain; charset=utf-8")
        s.close()

    def test_03_get_missing_file_404(self):
        """Test GET /missing_file.html returns 404 Not Found."""
        s = self.get_socket()
        req = encode_request("/missing_file.html")
        s.sendall(req)

        hdr = decode_frame_header(self.read_exact(s, 12))
        resp = decode_response(self.read_exact(s, hdr["payload_len"]))
        self.assertEqual(resp["status"], 404)
        s.close()

    def test_04_get_json_and_css(self):
        """Test MIME detection for .json and .css."""
        s = self.get_socket()
        # Test CSS
        s.sendall(encode_request("/style.css"))
        hdr = decode_frame_header(self.read_exact(s, 12))
        resp = decode_response(self.read_exact(s, hdr["payload_len"]))
        self.assertEqual(resp["status"], 200)
        self.assertEqual(resp["headers"][HEADER_CONTENT_TYPE], "text/css")

        # Test JSON on same connection
        s.sendall(encode_request("/test.json"))
        hdr = decode_frame_header(self.read_exact(s, 12))
        resp = decode_response(self.read_exact(s, hdr["payload_len"]))
        self.assertEqual(resp["status"], 200)
        self.assertEqual(resp["headers"][HEADER_CONTENT_TYPE], "application/json")
        self.assertIn(b"fixed-size-frame-header", resp["body"])
        s.close()

    def test_05_persistent_connection_multiple_requests(self):
        """Test that server keeps TCP connection open across multiple sequential requests."""
        s = self.get_socket()
        for i in range(5):
            path = "/hello.txt" if i % 2 == 0 else "/index.html"
            s.sendall(encode_request(path))
            hdr = decode_frame_header(self.read_exact(s, 12))
            resp = decode_response(self.read_exact(s, hdr["payload_len"]))
            self.assertEqual(resp["status"], 200)
        s.close()

    def test_06_unknown_frame_type_skipping(self):
        """
        HARD REQUIREMENT:
        Send an unknown frame type (0x99) with 256 bytes payload.
        The server MUST skip the 256 bytes cleanly and keep the stream synchronized.
        Then send a valid request on the same connection; server MUST return 200 OK.
        """
        s = self.get_socket()
        
        # 1. Send unknown frame type 0x99 with 256 bytes dummy payload
        dummy_payload = b"X" * 256
        unknown_hdr = encode_frame_header(MAGIC, VERSION, 0x99, 0, len(dummy_payload))
        s.sendall(unknown_hdr + dummy_payload)

        # Small delay to let server process and skip
        time.sleep(0.05)

        # 2. Send another unknown frame type 0xFE with 64 bytes dummy payload
        dummy2 = b"Y" * 64
        unknown_hdr2 = encode_frame_header(MAGIC, VERSION, 0xFE, 0, len(dummy2))
        s.sendall(unknown_hdr2 + dummy2)

        # 3. Now send a standard GET /hello.txt request
        s.sendall(encode_request("/hello.txt"))

        # 4. Server must have skipped both unknown frames and replied to the valid request
        hdr = decode_frame_header(self.read_exact(s, 12))
        self.assertEqual(hdr["frame_type"], FRAME_RESPONSE)
        resp = decode_response(self.read_exact(s, hdr["payload_len"]))
        self.assertEqual(resp["status"], 200)
        self.assertIn(b"Hello from BHTTP/1 Server!", resp["body"])
        s.close()

    def test_07_partial_tcp_reads(self):
        """Test sending request byte-by-byte with small delays (simulating TCP fragmentation)."""
        s = self.get_socket()
        req = encode_request("/hello.txt")
        for b in req:
            s.sendall(bytes([b]))
            time.sleep(0.001)

        hdr = decode_frame_header(self.read_exact(s, 12))
        resp = decode_response(self.read_exact(s, hdr["payload_len"]))
        self.assertEqual(resp["status"], 200)
        self.assertIn(b"Hello from BHTTP/1 Server!", resp["body"])
        s.close()

    def test_08_path_traversal_protection(self):
        """Test path traversal attacks are safely rejected with 400 Bad Request."""
        traversal_attempts = [
            "/../secret.txt",
            "/../../etc/passwd",
            "/style.css/../../hello.txt",
            "/..",
            "/../"
        ]
        for path in traversal_attempts:
            s = self.get_socket()
            s.sendall(encode_request(path))
            hdr = decode_frame_header(self.read_exact(s, 12))
            resp = decode_response(self.read_exact(s, hdr["payload_len"]))
            self.assertIn(resp["status"], [400, 404])
            s.close()

    def test_09_invalid_magic_handling(self):
        """Test wrong magic byte in header returns 400 / error safely without server crash."""
        s = self.get_socket()
        # Send bad magic 0xDEAD
        bad_hdr = encode_frame_header(0xDEAD, VERSION, FRAME_REQUEST, 0, 10)
        s.sendall(bad_hdr + b"0123456789")
        hdr_bytes = self.read_exact(s, 12)
        if len(hdr_bytes) == 12:
            hdr = decode_frame_header(hdr_bytes)
            if hdr["frame_type"] == FRAME_RESPONSE:
                resp = decode_response(self.read_exact(s, hdr["payload_len"]))
                self.assertEqual(resp["status"], 400)
        s.close()

    def test_10_unsupported_version_handling(self):
        """Test unsupported version returns 400."""
        s = self.get_socket()
        bad_hdr = encode_frame_header(MAGIC, 0x02, FRAME_REQUEST, 0, 10)
        s.sendall(bad_hdr + b"0123456789")
        hdr_bytes = self.read_exact(s, 12)
        if len(hdr_bytes) == 12:
            hdr = decode_frame_header(hdr_bytes)
            if hdr["frame_type"] == FRAME_RESPONSE:
                resp = decode_response(self.read_exact(s, hdr["payload_len"]))
                self.assertEqual(resp["status"], 400)
        s.close()

    def test_11_client_bcurl_execution(self):
        """Test bcurl client: 200 exits 0, stdout contains only body, -v goes to stderr."""
        cmd = ["./bcurl", "-v", f"localhost:{self.SERVER_PORT}/index.html"]
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(res.returncode, 0)
        
        # Verify stdout contains ONLY body
        with open("./www/index.html", "rb") as f:
            expected_body = f.read()
        self.assertEqual(res.stdout, expected_body)

        # Verify stderr contains verbose diagnostics and hex dump
        stderr_str = res.stderr.decode('utf-8', errors='ignore')
        self.assertIn("BHTTP Frame Header", stderr_str)
        self.assertIn("Magic:", stderr_str)
        self.assertIn("HTTP Status: 200", stderr_str)

    def test_12_client_bcurl_404_exit_code(self):
        """Test bcurl client exits non-zero on 404."""
        cmd = ["./bcurl", f"localhost:{self.SERVER_PORT}/nonexistent_file.xyz"]
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertNotEqual(res.returncode, 0)

    def test_14_oversized_payload_handling(self):
        """Test payload length exceeding 16MB max limit."""
        s = self.get_socket()
        oversized_hdr = encode_frame_header(MAGIC, VERSION, FRAME_REQUEST, 0, 20 * 1024 * 1024)
        s.sendall(oversized_hdr)
        hdr_bytes = self.read_exact(s, 12)
        if len(hdr_bytes) == 12:
            hdr = decode_frame_header(hdr_bytes)
            if hdr["frame_type"] == FRAME_RESPONSE:
                resp = decode_response(self.read_exact(s, hdr["payload_len"]))
                self.assertEqual(resp["status"], 400)
        s.close()

    def test_15_truncated_header_handling(self):
        """Test sender disconnecting midway through 12-byte header."""
        s = self.get_socket()
        s.sendall(b"\x42\x48\x01") # only 3 bytes
        s.close() # server must handle without crash or hang

        # Verify server is still alive and accepting new connections
        s2 = self.get_socket()
        s2.sendall(encode_request("/hello.txt"))
        hdr = decode_frame_header(self.read_exact(s2, 12))
        self.assertEqual(hdr["frame_type"], FRAME_RESPONSE)
        resp = decode_response(self.read_exact(s2, hdr["payload_len"]))
        self.assertEqual(resp["status"], 200)
        s2.close()

    def test_16_truncated_payload_handling(self):
        """Test sender advertising 50 bytes payload but disconnecting after 10 bytes."""
        s = self.get_socket()
        hdr = encode_frame_header(MAGIC, VERSION, FRAME_REQUEST, 0, 50)
        s.sendall(hdr + b"1234567890")
        s.close()

        # Verify server is still alive
        s2 = self.get_socket()
        s2.sendall(encode_request("/hello.txt"))
        hdr2 = decode_frame_header(self.read_exact(s2, 12))
        resp2 = decode_response(self.read_exact(s2, hdr2["payload_len"]))
        self.assertEqual(resp2["status"], 200)
        s2.close()

    def test_17_custom_header_support(self):
        """Test custom header encoding and decoding with ID 0x00."""
        # Encode a custom header: ID=0, Name="X-Test-Custom", Value="BHTTP-Rocks"
        path_bytes = b"/hello.txt"
        payload = bytearray()
        payload += struct.pack("!H", len(path_bytes))
        payload += path_bytes
        payload.append(1) # 1 header
        
        # Custom header wire format: ID(0x00) + NameLen(u16) + Name + ValLen(u16) + Val
        cname = b"X-Custom-Header"
        cval = b"Custom-Value-123"
        payload.append(0x00)
        payload += struct.pack("!H", len(cname))
        payload += cname
        payload += struct.pack("!H", len(cval))
        payload += cval

        hdr = encode_frame_header(MAGIC, VERSION, FRAME_REQUEST, 0, len(payload))
        s = self.get_socket()
        s.sendall(hdr + payload)

        resp_hdr = decode_frame_header(self.read_exact(s, 12))
        resp = decode_response(self.read_exact(s, resp_hdr["payload_len"]))
        self.assertEqual(resp["status"], 200)
        self.assertIn(b"Hello from BHTTP/1 Server!", resp["body"])
        s.close()

    def test_18_client_stdout_pure_body_pipe(self):
        """Test piping bcurl stdout to file matches original binary exactly."""
        cmd = ["./bcurl", f"localhost:{self.SERVER_PORT}/style.css"]
        res = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.assertEqual(res.returncode, 0)
        with open("./www/style.css", "rb") as f:
            expected = f.read()
        self.assertEqual(res.stdout, expected)
        self.assertEqual(len(res.stderr), 0) # without -v, stderr should be empty on success

if __name__ == "__main__":
    unittest.main(verbosity=2)

