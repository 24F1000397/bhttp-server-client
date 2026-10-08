# HTTP, IN BINARY: TWO TRACKS, ONE PROTOCOL (BHTTP/1)

**Course Project:** Computer Networks & Network Architecture  
**Protocol Version:** `BHTTP/1.0`  
**Specification Document:** [SPEC.md](SPEC.md)  
**Annotated Hex Dump:** [examples/annotated_hex.md](examples/annotated_hex.md)  

---

## 1. Project Overview

BHTTP/1 is a high-performance, deterministic, binary-framed transport protocol designed to replace text-based HTTP/1.1 parsing ambiguities with:
1. **Fixed-Size 12-Byte Frame Header:** Guarantees uniform header parsing and word-aligned framing.
2. **HPACK-Style Static Header Table:** Numbers the ten most common HTTP header names (`0x01` through `0x0A`) to single byte identifiers.
3. **Clean Unknown Frame Skipping:** Guarantees forward compatibility by skipping unhandled frame payloads (e.g. `BHTTP/2` extensions) while maintaining TCP stream synchronization.
4. **Single Persistent TCP Connection:** High-throughput keep-alive socket model serving multiple binary request/response frames over a single TCP connection.
5. **Path Traversal Security:** Canonical path resolution preventing unauthorized directory traversal out of the document root.

---

## 2. Directory Structure

```
├── Makefile                     # Build automation (all, clean, test, sanitize)
├── README.md                    # Project documentation
├── SPEC.md                      # Complete formal binary protocol specification
├── FINAL_CHECKLIST.md           # Formal project audit checklist
├── serve                        # Symlink to bin/serve (Track 1 server executable)
├── bcurl                        # Symlink to bin/bcurl (Track 2 client executable)
├── include/
│   └── protocol.h               # Wire-format constants, structures, and function declarations
├── src/
│   ├── protocol.c               # Binary frame encoding/decoding, header table & socket I/O engine
│   ├── serve.c                  # Track 1: Persistent BHTTP/1 HTTP file server implementation
│   └── bcurl.c                  # Track 2: BHTTP/1 command-line HTTP client implementation
├── bin/
│   ├── serve                    # Compiled server binary executable
│   └── bcurl                    # Compiled client binary executable
├── obj/                         # Object file build directory
├── docs/                        # Documentation
│   ├── ARCHITECTURE.md          # Architecture and design notes
│   └── SPEC.md                  # Protocol specification document
├── www/                         # Test document root directory
│   ├── index.html
│   ├── hello.txt
│   ├── test.json
│   └── style.css
├── tests/
│   └── test_suite.py            # Automated python test suite (17 comprehensive tests)
├── examples/
│   ├── request.bin              # Raw binary request frame
│   ├── response.bin             # Raw binary response frame
│   ├── request.hex              # Raw request hex dump
│   ├── response.hex             # Raw response hex dump
│   ├── request_response.hex     # Raw captured wire bytes
│   └── annotated_hex.md         # Byte-by-byte annotated hex dump
└── scripts/
    └── capture_hexdump.py       # Wire byte capture utility
```

---

## 3. Protocol Architecture Summary

### 3.1. Fixed 12-Byte Frame Header

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          Magic (0x4248)       |    Version    |   Frame Type  |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|     Flags     |                   Reserved                    |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                        Payload Length                         |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

- **Magic (`0x4248` = "BH"):** 2-byte protocol identifier (big-endian).
- **Version (`0x01`):** 1-byte major protocol version.
- **Frame Type:** 1 byte (`0x01` = REQ, `0x02` = RESP, `0x03` = GOAWAY).
- **Flags:** 1 byte (`0x01` = `FLAG_END_STREAM`).
- **Reserved:** 3 bytes (`0x000000`).
- **Payload Length:** 4-byte unsigned big-endian integer.

### 3.2. Static Header Indexing Table

| ID | Header Name | ID | Header Name |
| :---: | :--- | :---: | :--- |
| `0x01` | `Content-Type` | `0x06` | `Accept` |
| `0x02` | `Content-Length` | `0x07` | `User-Agent` |
| `0x03` | `Connection` | `0x08` | `Date` |
| `0x04` | `Server` | `0x09` | `Cache-Control` |
| `0x05` | `Host` | `0x0A` | `Content-Encoding` |

---

## 4. Build Instructions

### Standard Compilation
```bash
make clean
make
```
Compiles `src/serve.c`, `src/bcurl.c`, and `src/protocol.c` with flags `-Wall -Wextra -Wpedantic -O2 -std=c99`. Output binaries are stored in `bin/` (`bin/serve`, `bin/bcurl`) with root symlinks (`./serve`, `./bcurl`).

### AddressSanitizer & UndefinedBehaviorSanitizer
```bash
make sanitize
```
Compiles binaries with ASan/UBSan instrumentation to detect memory safety bugs and runtime undefined behavior.

---

## 5. Usage & Execution Examples

### 1. Launch Server (Track 1)
```bash
./serve ./www 9000
# or explicitly using the binary path:
./bin/serve ./www 9000
```

### 2. Fetch File Using Client (Track 2)
```bash
./bcurl localhost:9000/index.html
# or with explicit binary path:
./bin/bcurl localhost:9000/index.html
```

### 3. Verbose Diagnostic Output (`-v`)
```bash
./bcurl -v localhost:9000/index.html
```
Displays frame headers, field lengths, decoded header table entries, and payload hex dumps to `stderr` without polluting `stdout`.

### 4. Pipe Pure Body Output
```bash
./bcurl localhost:9000/hello.txt > output.txt
```
*Note:* Standard output (`stdout`) receives **only** the pure file body bytes, making it safe for binary downloads and piping.

---

## 6. Running the Test Suite

```bash
make test
# or directly running python:
python3 tests/test_suite.py
```

### Test Coverage Summary (17/17 Passing):
- [x] Basic file retrieval (`/index.html`, `/hello.txt`, `/style.css`, `/test.json`)
- [x] Content-Length and MIME type detection (`text/html`, `text/plain`, `application/json`, `text/css`)
- [x] 404 Not Found error responses
- [x] Persistent single TCP connection handling multiple requests sequentially
- [x] **Unknown frame type skipping** (skips unhandled frame types like `0x99`, remaining synchronized)
- [x] TCP fragmentation and partial read/write resilience
- [x] Path traversal security defense (`../` paths rejected with 400 Bad Request)
- [x] Frame validation: invalid magic, unsupported version, oversized payloads, truncated frames
- [x] Client CLI behavior: stdout pure body separation, `-v` stderr trace, exit code 0 on 200, non-zero on 404
