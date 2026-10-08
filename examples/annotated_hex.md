# Annotated BHTTP/1 Hex Dump

This document contains a complete, byte-by-byte annotated analysis of an actual network exchange captured from the BHTTP/1 implementation (requesting `/hello.txt`).

---

## 1. Request Frame (78 Bytes Total)

### Raw Hex Dump
```hex
0000  42 48 01 01 01 00 00 00  00 00 00 42 00 0a 2f 68  |BH.........B../h|
0010  65 6c 6c 6f 2e 74 78 74  04 05 00 0e 6c 6f 63 61  |ello.txt....loca|
0020  6c 68 6f 73 74 3a 39 38  37 36 07 00 13 62 63 75  |lhost:9876...bcu|
0030  72 6c 2f 31 2e 30 20 28  42 48 54 54 50 2f 31 29  |rl/1.0 (BHTTP/1)|
0040  06 00 03 2a 2f 2a 03 00  05 63 6c 6f 73 65        |...*/*...close  |
```

### Detailed Byte Breakdown

#### A. Fixed Frame Header (12 Bytes: Offsets `0x0000` - `0x000B`)

| Offset Range | Hex Bytes | Field Name | Width | Decoded Value & Explanation |
| :--- | :--- | :--- | :--- | :--- |
| `0x0000 - 0x0001` | `42 48` | **Magic** | 2 bytes | ASCII `"BH"` (`0x4248`), protocol identification |
| `0x0002` | `01` | **Version** | 1 byte | Version `1` (`BHTTP/1`) |
| `0x0003` | `01` | **Frame Type** | 1 byte | `0x01` = `REQUEST` frame |
| `0x0004` | `01` | **Flags** | 1 byte | `0x01` = `FLAG_END_STREAM` (indicates final frame on this stream) |
| `0x0005 - 0x0007` | `00 00 00` | **Reserved** | 3 bytes | `0x000000` (must be zero in V1, reserved for future extensions) |
| `0x0008 - 0x000B` | `00 00 00 42` | **Payload Length** | 4 bytes | `66` bytes (`0x42`), unsigned 32-bit big-endian integer |

#### B. Request Payload (66 Bytes: Offsets `0x000C` - `0x004D`)

| Offset Range | Hex Bytes | Field Name | Decoded Value & Explanation |
| :--- | :--- | :--- | :--- |
| `0x000C - 0x000D` | `00 0a` | **Path Length** | `10` bytes (uint16 big-endian) |
| `0x000E - 0x0017` | `2f 68 65 6c 6c 6f 2e 74 78 74` | **Path String** | `"/hello.txt"` (ASCII string) |
| `0x0018` | `04` | **Header Count** | `4` headers follow (uint8) |
| `0x0019` | `05` | **Header 1: ID** | Static ID `5` = `Host` |
| `0x001A - 0x001B` | `00 0e` | **Header 1: Value Length** | `14` bytes (uint16 big-endian) |
| `0x001C - 0x0029` | `6c 6f ... 37 36` | **Header 1: Value** | `"localhost:9876"` |
| `0x002A` | `07` | **Header 2: ID** | Static ID `7` = `User-Agent` |
| `0x002B - 0x002C` | `00 13` | **Header 2: Value Length** | `19` bytes (uint16 big-endian) |
| `0x002D - 0x003F` | `62 63 ... 29` | **Header 2: Value** | `"bcurl/1.0 (BHTTP/1)"` |
| `0x0040` | `06` | **Header 3: ID** | Static ID `6` = `Accept` |
| `0x0041 - 0x0042` | `00 03` | **Header 3: Value Length** | `3` bytes (uint16 big-endian) |
| `0x0043 - 0x0045` | `2a 2f 2a` | **Header 3: Value** | `"*/*"` |
| `0x0046` | `03` | **Header 4: ID** | Static ID `3` = `Connection` |
| `0x0047 - 0x0048` | `00 05` | **Header 4: Value Length** | `5` bytes (uint16 big-endian) |
| `0x0049 - 0x004D` | `63 6c 6f 73 65` | **Header 4: Value** | `"close"` |

---

## 2. Response Frame (161 Bytes Total)

### Raw Hex Dump
```hex
0000  42 48 01 02 01 00 00 00  00 00 00 95 00 c8 04 01  |BH..............|
0010  00 19 74 65 78 74 2f 70  6c 61 69 6e 3b 20 63 68  |..text/plain; ch|
0020  61 72 73 65 74 3d 75 74  66 2d 38 02 00 02 38 36  |arset=utf-8...86|
0030  04 00 10 42 48 54 54 50  2f 31 2e 30 20 53 65 72  |...BHTTP/1.0 Ser|
0040  76 65 72 03 00 05 63 6c  6f 73 65 48 65 6c 6c 6f  |ver...closeHello|
0050  20 66 72 6f 6d 20 42 48  54 54 50 2f 31 20 53 65  | from BHTTP/1 Se|
0060  72 76 65 72 21 0a 54 68  69 73 20 69 73 20 61 20  |rver!.This is a |
0070  70 6c 61 69 6e 20 74 65  78 74 20 66 69 6c 65 20  |plain text file |
0080  73 65 72 76 65 64 20 6f  76 65 72 20 62 69 6e 61  |served over bina|
0090  72 79 20 48 54 54 50 20  66 72 61 6d 69 6e 67 2e  |ry HTTP framing.|
00a0  0a                                                |.|
```

### Detailed Byte Breakdown

#### A. Fixed Frame Header (12 Bytes: Offsets `0x0000` - `0x000B`)

| Offset Range | Hex Bytes | Field Name | Width | Decoded Value & Explanation |
| :--- | :--- | :--- | :--- | :--- |
| `0x0000 - 0x0001` | `42 48` | **Magic** | 2 bytes | ASCII `"BH"` (`0x4248`) |
| `0x0002` | `01` | **Version** | 1 byte | Version `1` (`BHTTP/1`) |
| `0x0003` | `02` | **Frame Type** | 1 byte | `0x02` = `RESPONSE` frame |
| `0x0004` | `01` | **Flags** | 1 byte | `0x01` = `FLAG_END_STREAM` |
| `0x0005 - 0x0007` | `00 00 00` | **Reserved** | 3 bytes | `0x000000` |
| `0x0008 - 0x000B` | `00 00 00 95` | **Payload Length** | 4 bytes | `149` bytes (`0x95`), unsigned 32-bit big-endian integer |

#### B. Response Payload (149 Bytes: Offsets `0x000C` - `0x00A0`)

| Offset Range | Hex Bytes | Field Name | Decoded Value & Explanation |
| :--- | :--- | :--- | :--- |
| `0x000C - 0x000D` | `00 c8` | **Status Code** | `200` OK (`0x00C8`, uint16 big-endian) |
| `0x000E` | `04` | **Header Count** | `4` headers follow (uint8) |
| `0x000F` | `01` | **Header 1: ID** | Static ID `1` = `Content-Type` |
| `0x0010 - 0x0011` | `00 19` | **Header 1: Value Length** | `25` bytes (uint16 big-endian) |
| `0x0012 - 0x002A` | `74 65 ... 38` | **Header 1: Value** | `"text/plain; charset=utf-8"` |
| `0x002B` | `02` | **Header 2: ID** | Static ID `2` = `Content-Length` |
| `0x002C - 0x002D` | `00 02` | **Header 2: Value Length** | `2` bytes (uint16 big-endian) |
| `0x002E - 0x002F` | `38 36` | **Header 2: Value** | `"86"` (decimal ASCII string) |
| `0x0030` | `04` | **Header 3: ID** | Static ID `4` = `Server` |
| `0x0031 - 0x0032` | `00 10` | **Header 3: Value Length** | `16` bytes (uint16 big-endian) |
| `0x0033 - 0x0042` | `42 48 ... 65 72` | **Header 3: Value** | `"BHTTP/1.0 Server"` |
| `0x0043` | `03` | **Header 4: ID** | Static ID `3` = `Connection` |
| `0x0044 - 0x0045` | `00 05` | **Header 4: Value Length** | `5` bytes (uint16 big-endian) |
| `0x0046 - 0x004A` | `63 6c 6f 73 65` | **Header 4: Value** | `"close"` |
| `0x004B - 0x00A0` | `48 65 6c ... 0a` | **Body (86 bytes)** | `"Hello from BHTTP/1 Server!\nThis is a plain text file served over binary HTTP framing.\n"` |

---
**Verification**:
- Fixed Header: `12` bytes
- Payload: `149` bytes (Status: `2`, Header Count: `1`, Headers: `60`, Body: `86`)
- Total Frame Size: `12 + 149 = 161` bytes.
- `Content-Length` Header (`86`) matches the exact Body Length (`86` bytes).
