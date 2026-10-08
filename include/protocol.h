#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <sys/types.h>

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <unistd.h>
  #include <sys/socket.h>
  #include <netinet/in.h>
  #include <arpa/inet.h>
  #include <netdb.h>
#endif

/* ====================================================================
 * BHTTP/1 PROTOCOL SPECIFICATION CONSTANTS
 * ==================================================================== */

#define BHTTP_MAGIC_0            0x42  /* 'B' */
#define BHTTP_MAGIC_1            0x48  /* 'H' */
#define BHTTP_MAGIC_U16          0x4248
#define BHTTP_VERSION_1          0x01
#define BHTTP_HEADER_SIZE        12
#define BHTTP_MAX_PAYLOAD_SIZE   (16 * 1024 * 1024) /* 16 MB max payload limit */

/* Frame Types */
#define BHTTP_FRAME_REQUEST      0x01
#define BHTTP_FRAME_RESPONSE     0x02
#define BHTTP_FRAME_GOAWAY       0x03

/* Flags */
#define BHTTP_FLAG_NONE          0x00
#define BHTTP_FLAG_END_STREAM    0x01

/* Static Header Table IDs (1-10) */
#define BHTTP_HEADER_CUSTOM            0x00
#define BHTTP_HEADER_CONTENT_TYPE      0x01
#define BHTTP_HEADER_CONTENT_LENGTH    0x02
#define BHTTP_HEADER_CONNECTION        0x03
#define BHTTP_HEADER_SERVER            0x04
#define BHTTP_HEADER_HOST              0x05
#define BHTTP_HEADER_ACCEPT            0x06
#define BHTTP_HEADER_USER_AGENT        0x07
#define BHTTP_HEADER_DATE              0x08
#define BHTTP_HEADER_CACHE_CONTROL     0x09
#define BHTTP_HEADER_CONTENT_ENCODING  0x0A
#define BHTTP_STATIC_HEADER_COUNT      10

/* HTTP Status Codes */
#define BHTTP_STATUS_OK                  200
#define BHTTP_STATUS_BAD_REQUEST         400
#define BHTTP_STATUS_FORBIDDEN           403
#define BHTTP_STATUS_NOT_FOUND           404
#define BHTTP_STATUS_INTERNAL_ERROR      500

/* ====================================================================
 * STRUCTURES
 * ==================================================================== */

/* 12-byte fixed header representation */
typedef struct {
    uint16_t magic;           /* 0x4248 */
    uint8_t  version;         /* 0x01 */
    uint8_t  frame_type;      /* 0x01=REQ, 0x02=RESP, 0x03=GOAWAY, ... */
    uint8_t  flags;           /* Bit 0: END_STREAM */
    uint8_t  reserved[3];     /* Must be 0x000000 */
    uint32_t payload_len;     /* Length in bytes following header */
} bhttp_frame_header_t;

/* Single Header Name-Value pair */
typedef struct {
    uint8_t  id;              /* 1..10 for static table, 0 for custom */
    char    *custom_name;     /* NULL if id != 0 */
    char    *value;           /* NULL-terminated string */
} bhttp_header_t;

#define BHTTP_MAX_HEADERS 64

/* Decoded Request */
typedef struct {
    char           *path;
    uint8_t         header_count;
    bhttp_header_t  headers[BHTTP_MAX_HEADERS];
} bhttp_request_t;

/* Decoded Response */
typedef struct {
    uint16_t        status_code;
    uint8_t         header_count;
    bhttp_header_t  headers[BHTTP_MAX_HEADERS];
    uint8_t        *body;
    size_t          body_len;
} bhttp_response_t;

/* Dynamic Buffer for framing/serialization */
typedef struct {
    uint8_t *data;
    size_t   len;
    size_t   cap;
} bhttp_buf_t;

/* ====================================================================
 * API FUNCTIONS
 * ==================================================================== */

/* Buffer helpers */
void bhttp_buf_init(bhttp_buf_t *buf);
void bhttp_buf_free(bhttp_buf_t *buf);
bool bhttp_buf_append(bhttp_buf_t *buf, const void *data, size_t len);
bool bhttp_buf_append_u8(bhttp_buf_t *buf, uint8_t val);
bool bhttp_buf_append_u16(bhttp_buf_t *buf, uint16_t val);
bool bhttp_buf_append_u32(bhttp_buf_t *buf, uint32_t val);

/* Static Header Table Lookup */
const char *bhttp_header_id_to_name(uint8_t id);
uint8_t     bhttp_header_name_to_id(const char *name);

/* Frame Header Serialization / Deserialization */
void bhttp_encode_frame_header(uint8_t *dest, const bhttp_frame_header_t *hdr);
bool bhttp_decode_frame_header(const uint8_t *src, bhttp_frame_header_t *hdr);

/* Request Serialization / Deserialization */
bool bhttp_encode_request(bhttp_buf_t *out, const char *path, const bhttp_header_t *headers, uint8_t header_count, uint8_t flags);
bool bhttp_decode_request_payload(const uint8_t *payload, size_t len, bhttp_request_t *req);
void bhttp_request_free(bhttp_request_t *req);

/* Response Serialization / Deserialization */
bool bhttp_encode_response(bhttp_buf_t *out, uint16_t status_code, const bhttp_header_t *headers, uint8_t header_count, const uint8_t *body, size_t body_len, uint8_t flags);
bool bhttp_decode_response_payload(const uint8_t *payload, size_t len, bhttp_response_t *resp);
void bhttp_response_free(bhttp_response_t *resp);

/* Robust Socket I/O Helpers */
ssize_t bhttp_read_exact(int fd, void *buf, size_t count);
ssize_t bhttp_write_all(int fd, const void *buf, size_t count);
bool    bhttp_skip_bytes(int fd, size_t count);

/* Debugging / Hex Dump */
void bhttp_hexdump(FILE *stream, const char *title, const uint8_t *data, size_t len);
void bhttp_dump_frame(FILE *stream, const bhttp_frame_header_t *hdr, const uint8_t *payload);

#endif /* PROTOCOL_H */
