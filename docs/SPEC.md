# BHTTP/1 Protocol Specification (Binary HTTP / Version 1)

**Status:** Final Standard  
**Author:** Network Architecture Project  
**Date:** October 2026  
**Document Format:** 2-Page Standard Specification  

---

## 1. Abstract and Design Philosophy

**BHTTP/1** (Binary HTTP Version 1) is an efficient, deterministic, binary-framed application layer transport protocol designed to replace text-based HTTP/1.1 parsing ambiguities with fixed-size binary framing, numeric static header indexing (HPACK-style), and persistent single-TCP-connection multiplexing.

### Key Architectural Invariants:
1. **Deterministic Framing:** Every transmission unit begins with a 12-byte fixed-size frame header.
2. **Network Byte Order:** All multi-byte numeric fields are strictly big-endian (`MSB` first).
3. **Forward Extensibility:** A receiver encountering an unknown frame type **MUST** cleanly read and discard exactly `Payload Length` bytes, remaining fully synchronized on the TCP byte stream.
4. **Connection Efficiency:** Single persistent TCP connection per client session with keep-alive semantics.

---

## 2. Fixed-Size Frame Header Layout

Every frame on the wire begins with an immutable **12-byte header**:

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
|                     Payload Data (...) ...                    |
```

### Header Field Specification:

| Offset | Size (Bytes) | Field Name | Type | Description |
| :--- | :--- | :--- | :--- | :--- |
| `0` | `2` | **Magic** | `uint16` | Fixed constant `0x4248` (ASCII `"BH"`). Identifies protocol framing and catches stream misalignment early. |
| `2` | `1` | **Version** | `uint8` | Protocol version (`0x01` for BHTTP/1). Receivers reject unsupported major versions with `400 Bad Request`. |
| `3` | `1` | **Frame Type** | `uint8` | Identifies frame semantics (`0x01` = `REQUEST`, `0x02` = `RESPONSE`, `0x03` = `GOAWAY`). |
| `4` | `1` | **Flags** | `uint8` | Bitfield flags. Bit 0 (`0x01`): `FLAG_END_STREAM` (indicates sender will close connection). Bits 1-7: Reserved. |
| `5` | `3` | **Reserved** | `3 bytes` | Must be sent as `0x000000`. Ignored on receipt for forward compatibility. |
| `8` | `4` | **Payload Length** | `uint32` | Exact size (in bytes) of payload immediately following the 12-byte header. Maximum limit: `16,777,216` bytes (16 MB). |

---

## 3. Static Header Compression Table & Custom Headers

Headers are encoded using an HPACK-inspired mechanism: common HTTP header names are mapped to 1-byte numeric identifiers; custom/extended header names are length-prefixed.

### Static Header Table (IDs `1` - `10`):

| ID (`uint8`) | Header Name | Primary Context | Canonical Example |
| :---: | :--- | :--- | :--- |
| `0x01` | `Content-Type` | Response / Request | `text/html; charset=utf-8` |
| `0x02` | `Content-Length` | Response / Request | `1024` |
| `0x03` | `Connection` | Request / Response | `keep-alive`, `close` |
| `0x04` | `Server` | Response | `BHTTP/1.0 Server` |
| `0x05` | `Host` | Request | `localhost:9000` |
| `0x06` | `Accept` | Request | `*/*`, `text/html` |
| `0x07` | `User-Agent` | Request | `bcurl/1.0` |
| `0x08` | `Date` | Response | `Wed, 07 Oct 2026 10:00:00 GMT` |
| `0x09` | `Cache-Control` | Request / Response | `no-cache`, `max-age=3600` |
| `0x0A` | `Content-Encoding`| Response | `identity`, `gzip` |

### Header Encoding Wire Formats:

- **Indexed Header (`ID != 0x00`):**
  ```
  +--------------+-------------------+--------------------+
  | ID (1 byte)  | Val Len (2 bytes) | Value (N bytes)    |
  +--------------+-------------------+--------------------+
  ```
- **Custom Header (`ID == 0x00`):**
  ```
  +--------+--------------------+---------------+-------------------+----------------+
  | 0x00   | Name Len (2 bytes) | Name (M bytes)| Val Len (2 bytes) | Value (N bytes)|
  +--------+--------------------+---------------+-------------------+----------------+
  ```

---

## 4. Frame Payload Specifications

### 4.1. Request Frame (`Frame Type = 0x01`)

```
+-------------------+---------------------+--------------------+----------------------+
| Path Len (2 bytes)| Path String (N bytes)| Header Count (1 B) | Encoded Headers (...) |
+-------------------+---------------------+--------------------+----------------------+
```
- **Path Length:** 2-byte unsigned big-endian integer.
- **Path String:** UTF-8 / ASCII request path (e.g., `/index.html`).
- **Header Count:** 1-byte integer ($0 \le N \le 64$).
- **Encoded Headers:** Sequence of $N$ encoded headers.

### 4.2. Response Frame (`Frame Type = 0x02`)

```
+--------------------+--------------------+----------------------+-------------------+
| Status Code (2 B)  | Header Count (1 B) | Encoded Headers (...) | Body Data (...)   |
+--------------------+--------------------+----------------------+-------------------+
```
- **Status Code:** 2-byte unsigned big-endian integer (`200`, `400`, `403`, `404`, `500`).
- **Header Count:** 1-byte integer ($0 \le N \le 64$).
- **Encoded Headers:** Sequence of $N$ encoded headers.
- **Body Data:** Raw body bytes. Length is implicitly calculated as:  
  $$\text{Body Length} = \text{Payload Length} - (3 + \text{Total Headers Length})$$

---

## 5. Extensibility & Unknown Frame Handling

**Mandatory Rule:** If any receiver (client or server) parses a 12-byte header with a `Frame Type` not recognized by its version:
1. The receiver **MUST NOT** close the connection or fail parsing.
2. The receiver **MUST** read and discard exactly `Payload Length` bytes from the TCP socket.
3. The receiver resumes parsing immediately at the start of the next 12-byte header.

This guarantees forward compatibility with future BHTTP/2 frame types (such as `SETTINGS`, `PING`, or `WINDOW_UPDATE`).

---

## 6. Connection Semantics & TCP Socket Handling

- **Persistence:** Servers maintain connections open (`keep-alive`) across multiple sequential requests.
- **Stream Termination:** Either endpoint may assert `FLAG_END_STREAM` (`0x01`) in the frame header to signal connection closure after current frame delivery.
- **TCP Byte Streaming:** Sockets must use exact loop readers (`read_exact`) and writers (`write_all`) to prevent truncation across TCP packet fragmentation boundaries.

---

## 7. Security and Path Resolution Rules

1. **Document Root Isolation:** Servers must resolve requested paths using canonical root validation (`realpath`).
2. **Directory Traversal Defense:** Paths containing `..`, null bytes, or attempting to escape the configured document root are rejected immediately with `400 Bad Request` or `404 Not Found`.
3. **MIME Safety:** Unrecognized extensions default to `application/octet-stream`.

---

## 8. Design Rationale & Width Defense

1. **Why 12-byte fixed header?**  
   12 bytes is 32-bit word aligned and provides the minimal overhead necessary to combine protocol identification, versioning, frame typing, flags, reserved expansion, and exact payload demarcation.
2. **Why 2-byte Magic (`0x4248`)?**  
   Fast binary identification to distinguish BHTTP from ASCII HTTP/1.1 or random traffic; enables instant synchronization check before reading payloads.
3. **Why 4-byte Payload Length?**  
   Allows payloads up to 4 GB (bounded by implementation limit of 16 MB) while maintaining uniform word alignment and constant offset parsing.
4. **Why Numeric Header IDs?**  
   Eliminates repetitive string transmission of standard headers (e.g. `Content-Type`, `Content-Length`), reducing wire payload size by up to 70% while supporting custom headers via `0x00`.
