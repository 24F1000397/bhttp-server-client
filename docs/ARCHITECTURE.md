# BHTTP/1 Architecture & Engineering Design

This document details the architectural design choices, subsystem interfaces, and engineering defenses of the **BHTTP/1** implementation.

---

## 1. System Architecture Overview

The system consists of two primary operational tracks communicating over a unified binary transport layer:

```
+-------------------------------------------------------------+
|                      BHTTP/1 Ecosystem                      |
+-------------------------------------------------------------+
|                                                             |
|   +--------------------+               +----------------+   |
|   |   Track 2: Client  |               | Track 1: Server|   |
|   |     (./bcurl)      |               |    (./serve)   |   |
|   +---------+----------+               +--------+-------+   |
|             |                                   |           |
|             |  12-Byte Fixed Header Binary Frames|          |
|             +-----------------------------------+           |
|                               |                             |
|             +-----------------+-----------------+           |
|             |      Shared Protocol Engine       |           |
|             |     (protocol.h / protocol.c)     |           |
|             +-----------------+-----------------+           |
|                               |                             |
|             +-----------------+-----------------+           |
|             |    HPACK-Style Header Static Table|           |
|             |    Robust Socket Helpers (IO)     |           |
|             |    Unknown Frame Extensibility    |           |
|             |    Canonical Path Security        |           |
|             +-----------------------------------+           |
|                                                             |
+-------------------------------------------------------------+
```

---

## 2. Subsystem Descriptions

### 2.1. Shared Protocol Engine (`include/protocol.h`, `src/protocol.c`)
- **Fixed-Size Framing:** Encodes and decodes 12-byte framing headers with big-endian byte ordering.
- **Header Static Table:** Fast bidirectional lookup between 10 standard HTTP headers and 1-byte numeric identifiers.
- **Socket I/O Guards:** `bhttp_read_exact` and `bhttp_write_all` loops ensure complete frame transmission regardless of TCP segment boundary fragmentation.
- **Skipping Engine:** `bhttp_skip_bytes` discards unknown payload frames chunk-by-chunk without excessive buffer allocation.

### 2.2. Track 1: Persistent File Server (`src/serve.c`)
- **Connection Persistence:** Keeps client TCP connections open across sequential requests until peer disconnect or `FLAG_END_STREAM`.
- **Safe Path Containment:** Validates requested paths using canonical `realpath()` resolution to eliminate directory traversal attacks (`../`).
- **Dynamic Content-Type:** MIME detection based on file extensions.

### 2.3. Track 2: Binary Client (`src/bcurl.c`)
- **Single Connection Guarantee:** Establishes exactly one TCP connection per transaction.
- **Clean Output Stream Separation:** Transmits raw binary body to `stdout` and diagnostic verbose/hex information to `stderr`.
- **Status Exit Codes:** Returns code `0` on 2xx responses and non-zero on 4xx/5xx responses.

---

## 3. Directory Layout Standard

- `include/`: Header interfaces (`protocol.h`).
- `src/`: Core C implementations (`protocol.c`, `serve.c`, `bcurl.c`, `bserve.c`).
- `www/`: Static test document root.
- `tests/`: Automated test suite.
- `examples/`: Raw hex traces and byte-by-byte annotations.
- `docs/`: Architectural guides and formal specifications.
