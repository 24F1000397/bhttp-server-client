# FINAL SELF-AUDIT CHECKLIST: BHTTP/1 PROJECT

| Status | Project Requirement | Verification Method |
| :---: | :--- | :--- |
| **[PASS]** | Fixed-size binary header | 12-byte header struct defined in `protocol.h`, decoded in `protocol.c` |
| **[PASS]** | Magic field | `0x4248` ("BH") validated on all incoming frames |
| **[PASS]** | Version field | `0x01` verified on all incoming frames; invalid versions rejected |
| **[PASS]** | Frame type | Explicit `0x01` (REQ), `0x02` (RESP), `0x03` (GOAWAY) |
| **[PASS]** | Flags | `FLAG_END_STREAM` (`0x01`) supported |
| **[PASS]** | Payload length | 32-bit unsigned big-endian length prefix; bounded to 16 MB max |
| **[PASS]** | Big-endian encoding | Pure shift-based portable big-endian byte order |
| **[PASS]** | Request frame | Length-prefixed path, header count, and serialized header fields |
| **[PASS]** | Response frame | 2-byte status code, header count, serialized headers, and body |
| **[PASS]** | Ten numbered headers | Static table `0x01` to `0x0A` covering `Content-Type` through `Content-Encoding` |
| **[PASS]** | Length-prefixed values | All header values and custom header names are 16-bit length-prefixed |
| **[PASS]** | Unknown frame skipping | Receivers cleanly read & discard `Payload Length` bytes; tested with `0x99` & `0xFE` |
| **[PASS]** | Version handling | Unsupported versions return `400 Bad Request` safely |
| **[PASS]** | 400 handling | Malformed headers/frames, bad magic, and traversal attempts return 400 |
| **[PASS]** | 404 handling | Missing files return `404 Not Found` with clean error body |
| **[PASS]** | 500 handling | Server internal errors handled safely |
| **[PASS]** | Keep-alive connection | Server loop retains TCP socket across sequential requests |
| **[PASS]** | Client one-connection requirement | `bcurl` connects exactly once per run and terminates cleanly |
| **[PASS]** | stdout body only | Standard output receives only raw response body bytes |
| **[PASS]** | verbose output | `-v` flag directs structured hex and debug logs to `stderr` |
| **[PASS]** | Path traversal protection | `realpath` containment verifies requested path cannot escape document root |
| **[PASS]** | Partial TCP reads | Loop-based `bhttp_read_exact` handles fragmented socket packets |
| **[PASS]** | Partial TCP writes | Loop-based `bhttp_write_all` handles partial buffer writes |
| **[PASS]** | Malformed-frame handling | Truncated inputs, corrupt bytes, and oversized lengths fail safely |
| **[PASS]** | Test suite | 17 automated tests passing in `tests/test_suite.py` |
| **[PASS]** | Actual hex dump | Captured live wire bytes in `examples/request_response.hex` |
| **[PASS]** | Annotated hex dump | Full byte-by-byte explanation in `examples/annotated_hex.md` |
| **[PASS]** | Two-page specification | Complete standard specification in `SPEC.md` |
| **[PASS]** | README | Full project documentation in `README.md` |
| **[PASS]** | Build works from clean state | `make clean && make` compiles with 0 warnings under `-Wall -Wextra -Wpedantic` |
| **[PASS]** | Sanitizer clean | Clean build & 17-test runtime execution under `-fsanitize=address,undefined` |
