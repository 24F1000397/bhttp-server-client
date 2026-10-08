#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#ifdef _WIN32
  #pragma comment(lib, "ws2_32.lib")
#endif

/* Static header table mapping */
static const char *STATIC_HEADERS[BHTTP_STATIC_HEADER_COUNT + 1] = {
    "",                 /* 0: Custom / Not in table */
    "content-type",     /* 1 */
    "content-length",   /* 2 */
    "connection",       /* 3 */
    "server",           /* 4 */
    "host",             /* 5 */
    "accept",           /* 6 */
    "user-agent",       /* 7 */
    "date",             /* 8 */
    "cache-control",    /* 9 */
    "content-encoding"  /* 10 */
};

/* Case-insensitive string comparison */
static int strcasecmp_portable(const char *s1, const char *s2) {
    while (*s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2) return c1 - c2;
        s1++;
        s2++;
    }
    return tolower((unsigned char)*s1) - tolower((unsigned char)*s2);
}

const char *bhttp_header_id_to_name(uint8_t id) {
    if (id >= 1 && id <= BHTTP_STATIC_HEADER_COUNT) {
        return STATIC_HEADERS[id];
    }
    return NULL;
}

uint8_t bhttp_header_name_to_id(const char *name) {
    if (!name) return BHTTP_HEADER_CUSTOM;
    for (uint8_t i = 1; i <= BHTTP_STATIC_HEADER_COUNT; i++) {
        if (strcasecmp_portable(name, STATIC_HEADERS[i]) == 0) {
            return i;
        }
    }
    return BHTTP_HEADER_CUSTOM;
}

/* ====================================================================
 * BUFFER IMPLEMENTATION
 * ==================================================================== */

void bhttp_buf_init(bhttp_buf_t *buf) {
    buf->data = NULL;
    buf->len = 0;
    buf->cap = 0;
}

void bhttp_buf_free(bhttp_buf_t *buf) {
    if (buf->data) {
        free(buf->data);
        buf->data = NULL;
    }
    buf->len = 0;
    buf->cap = 0;
}

bool bhttp_buf_append(bhttp_buf_t *buf, const void *data, size_t len) {
    if (len == 0) return true;
    if (buf->len + len > buf->cap) {
        size_t new_cap = buf->cap == 0 ? 256 : buf->cap * 2;
        while (new_cap < buf->len + len) {
            new_cap *= 2;
        }
        uint8_t *new_data = (uint8_t *)realloc(buf->data, new_cap);
        if (!new_data) return false;
        buf->data = new_data;
        buf->cap = new_cap;
    }
    if (data) {
        memcpy(buf->data + buf->len, data, len);
    }
    buf->len += len;
    return true;
}

bool bhttp_buf_append_u8(bhttp_buf_t *buf, uint8_t val) {
    return bhttp_buf_append(buf, &val, 1);
}

bool bhttp_buf_append_u16(bhttp_buf_t *buf, uint16_t val) {
    uint8_t b[2];
    b[0] = (uint8_t)((val >> 8) & 0xFF);
    b[1] = (uint8_t)(val & 0xFF);
    return bhttp_buf_append(buf, b, 2);
}

bool bhttp_buf_append_u32(bhttp_buf_t *buf, uint32_t val) {
    uint8_t b[4];
    b[0] = (uint8_t)((val >> 24) & 0xFF);
    b[1] = (uint8_t)((val >> 16) & 0xFF);
    b[2] = (uint8_t)((val >> 8) & 0xFF);
    b[3] = (uint8_t)(val & 0xFF);
    return bhttp_buf_append(buf, b, 4);
}

/* ====================================================================
 * FRAME HEADER SERIALIZATION / DESERIALIZATION
 * ==================================================================== */

void bhttp_encode_frame_header(uint8_t *dest, const bhttp_frame_header_t *hdr) {
    /* Offset 0..1: Magic (0x4248) */
    dest[0] = (uint8_t)((hdr->magic >> 8) & 0xFF);
    dest[1] = (uint8_t)(hdr->magic & 0xFF);
    /* Offset 2: Version */
    dest[2] = hdr->version;
    /* Offset 3: Frame Type */
    dest[3] = hdr->frame_type;
    /* Offset 4: Flags */
    dest[4] = hdr->flags;
    /* Offset 5..7: Reserved */
    dest[5] = hdr->reserved[0];
    dest[6] = hdr->reserved[1];
    dest[7] = hdr->reserved[2];
    /* Offset 8..11: Payload Length (Big Endian) */
    dest[8]  = (uint8_t)((hdr->payload_len >> 24) & 0xFF);
    dest[9]  = (uint8_t)((hdr->payload_len >> 16) & 0xFF);
    dest[10] = (uint8_t)((hdr->payload_len >> 8) & 0xFF);
    dest[11] = (uint8_t)(hdr->payload_len & 0xFF);
}

bool bhttp_decode_frame_header(const uint8_t *src, bhttp_frame_header_t *hdr) {
    if (!src || !hdr) return false;
    hdr->magic = ((uint16_t)src[0] << 8) | (uint16_t)src[1];
    hdr->version = src[2];
    hdr->frame_type = src[3];
    hdr->flags = src[4];
    hdr->reserved[0] = src[5];
    hdr->reserved[1] = src[6];
    hdr->reserved[2] = src[7];
    hdr->payload_len = ((uint32_t)src[8] << 24) |
                       ((uint32_t)src[9] << 16) |
                       ((uint32_t)src[10] << 8) |
                       (uint32_t)src[11];
    return true;
}

/* ====================================================================
 * HEADER ENCODING / DECODING HELPERS
 * ==================================================================== */

static bool encode_headers(bhttp_buf_t *out, const bhttp_header_t *headers, uint8_t count) {
    if (!bhttp_buf_append_u8(out, count)) return false;
    for (uint8_t i = 0; i < count; i++) {
        uint8_t id = headers[i].id;
        const char *val = headers[i].value ? headers[i].value : "";
        size_t val_len = strlen(val);
        if (val_len > 0xFFFF) return false; /* Value length overflow */

        if (id == BHTTP_HEADER_CUSTOM) {
            const char *cname = headers[i].custom_name ? headers[i].custom_name : "";
            size_t name_len = strlen(cname);
            if (name_len > 0xFFFF) return false;

            if (!bhttp_buf_append_u8(out, BHTTP_HEADER_CUSTOM)) return false;
            if (!bhttp_buf_append_u16(out, (uint16_t)name_len)) return false;
            if (!bhttp_buf_append(out, cname, name_len)) return false;
            if (!bhttp_buf_append_u16(out, (uint16_t)val_len)) return false;
            if (!bhttp_buf_append(out, val, val_len)) return false;
        } else {
            if (!bhttp_buf_append_u8(out, id)) return false;
            if (!bhttp_buf_append_u16(out, (uint16_t)val_len)) return false;
            if (!bhttp_buf_append(out, val, val_len)) return false;
        }
    }
    return true;
}

static bool decode_headers(const uint8_t *payload, size_t len, size_t *offset, bhttp_header_t *headers_out, uint8_t *count_out) {
    if (*offset >= len) return false;
    uint8_t count = payload[(*offset)++];
    if (count > BHTTP_MAX_HEADERS) return false;

    for (uint8_t i = 0; i < count; i++) {
        if (*offset >= len) return false;
        uint8_t id = payload[(*offset)++];
        headers_out[i].id = id;
        headers_out[i].custom_name = NULL;
        headers_out[i].value = NULL;

        if (id == BHTTP_HEADER_CUSTOM) {
            if (*offset + 2 > len) return false;
            uint16_t name_len = ((uint16_t)payload[*offset] << 8) | (uint16_t)payload[*offset + 1];
            *offset += 2;

            if (*offset + name_len > len) return false;
            char *cname = (char *)malloc(name_len + 1);
            if (!cname) return false;
            memcpy(cname, payload + *offset, name_len);
            cname[name_len] = '\0';
            headers_out[i].custom_name = cname;
            *offset += name_len;

            if (*offset + 2 > len) {
                free(cname);
                headers_out[i].custom_name = NULL;
                return false;
            }
            uint16_t val_len = ((uint16_t)payload[*offset] << 8) | (uint16_t)payload[*offset + 1];
            *offset += 2;

            if (*offset + val_len > len) {
                free(cname);
                headers_out[i].custom_name = NULL;
                return false;
            }
            char *val = (char *)malloc(val_len + 1);
            if (!val) {
                free(cname);
                headers_out[i].custom_name = NULL;
                return false;
            }
            memcpy(val, payload + *offset, val_len);
            val[val_len] = '\0';
            headers_out[i].value = val;
            *offset += val_len;
        } else {
            if (id > BHTTP_STATIC_HEADER_COUNT) {
                /* Unknown non-custom static header ID */
                return false;
            }
            if (*offset + 2 > len) return false;
            uint16_t val_len = ((uint16_t)payload[*offset] << 8) | (uint16_t)payload[*offset + 1];
            *offset += 2;

            if (*offset + val_len > len) return false;
            char *val = (char *)malloc(val_len + 1);
            if (!val) return false;
            memcpy(val, payload + *offset, val_len);
            val[val_len] = '\0';
            headers_out[i].value = val;
            *offset += val_len;
        }
    }
    *count_out = count;
    return true;
}

/* ====================================================================
 * REQUEST ENCODE / DECODE
 * ==================================================================== */

bool bhttp_encode_request(bhttp_buf_t *out, const char *path, const bhttp_header_t *headers, uint8_t header_count, uint8_t flags) {
    if (!out || !path) return false;
    size_t path_len = strlen(path);
    if (path_len > 0xFFFF) return false;

    /* Build payload in a temp buffer */
    bhttp_buf_t payload;
    bhttp_buf_init(&payload);

    if (!bhttp_buf_append_u16(&payload, (uint16_t)path_len)) {
        bhttp_buf_free(&payload);
        return false;
    }
    if (!bhttp_buf_append(&payload, path, path_len)) {
        bhttp_buf_free(&payload);
        return false;
    }
    if (!encode_headers(&payload, headers, header_count)) {
        bhttp_buf_free(&payload);
        return false;
    }

    if (payload.len > BHTTP_MAX_PAYLOAD_SIZE) {
        bhttp_buf_free(&payload);
        return false;
    }

    /* Build frame header */
    bhttp_frame_header_t hdr;
    hdr.magic = BHTTP_MAGIC_U16;
    hdr.version = BHTTP_VERSION_1;
    hdr.frame_type = BHTTP_FRAME_REQUEST;
    hdr.flags = flags;
    hdr.reserved[0] = 0;
    hdr.reserved[1] = 0;
    hdr.reserved[2] = 0;
    hdr.payload_len = (uint32_t)payload.len;

    uint8_t hdr_bytes[BHTTP_HEADER_SIZE];
    bhttp_encode_frame_header(hdr_bytes, &hdr);

    if (!bhttp_buf_append(out, hdr_bytes, BHTTP_HEADER_SIZE)) {
        bhttp_buf_free(&payload);
        return false;
    }
    if (!bhttp_buf_append(out, payload.data, payload.len)) {
        bhttp_buf_free(&payload);
        return false;
    }

    bhttp_buf_free(&payload);
    return true;
}

bool bhttp_decode_request_payload(const uint8_t *payload, size_t len, bhttp_request_t *req) {
    if (!payload || !req) return false;
    memset(req, 0, sizeof(*req));

    if (len < 2) return false;
    uint16_t path_len = ((uint16_t)payload[0] << 8) | (uint16_t)payload[1];
    size_t offset = 2;

    if (offset + path_len > len) return false;
    req->path = (char *)malloc(path_len + 1);
    if (!req->path) return false;
    memcpy(req->path, payload + offset, path_len);
    req->path[path_len] = '\0';
    offset += path_len;

    if (offset >= len) {
        /* No headers included (truncated) */
        free(req->path);
        req->path = NULL;
        return false;
    }

    if (!decode_headers(payload, len, &offset, req->headers, &req->header_count)) {
        bhttp_request_free(req);
        return false;
    }

    if (offset != len) {
        /* Extraneous unexpected bytes in request payload */
        bhttp_request_free(req);
        return false;
    }

    return true;
}

void bhttp_request_free(bhttp_request_t *req) {
    if (!req) return;
    if (req->path) {
        free(req->path);
        req->path = NULL;
    }
    for (uint8_t i = 0; i < req->header_count; i++) {
        if (req->headers[i].custom_name) {
            free(req->headers[i].custom_name);
            req->headers[i].custom_name = NULL;
        }
        if (req->headers[i].value) {
            free(req->headers[i].value);
            req->headers[i].value = NULL;
        }
    }
    req->header_count = 0;
}

/* ====================================================================
 * RESPONSE ENCODE / DECODE
 * ==================================================================== */

bool bhttp_encode_response(bhttp_buf_t *out, uint16_t status_code, const bhttp_header_t *headers, uint8_t header_count, const uint8_t *body, size_t body_len, uint8_t flags) {
    if (!out) return false;

    bhttp_buf_t payload;
    bhttp_buf_init(&payload);

    if (!bhttp_buf_append_u16(&payload, status_code)) {
        bhttp_buf_free(&payload);
        return false;
    }
    if (!encode_headers(&payload, headers, header_count)) {
        bhttp_buf_free(&payload);
        return false;
    }
    if (body_len > 0 && body) {
        if (!bhttp_buf_append(&payload, body, body_len)) {
            bhttp_buf_free(&payload);
            return false;
        }
    }

    if (payload.len > BHTTP_MAX_PAYLOAD_SIZE) {
        bhttp_buf_free(&payload);
        return false;
    }

    bhttp_frame_header_t hdr;
    hdr.magic = BHTTP_MAGIC_U16;
    hdr.version = BHTTP_VERSION_1;
    hdr.frame_type = BHTTP_FRAME_RESPONSE;
    hdr.flags = flags;
    hdr.reserved[0] = 0;
    hdr.reserved[1] = 0;
    hdr.reserved[2] = 0;
    hdr.payload_len = (uint32_t)payload.len;

    uint8_t hdr_bytes[BHTTP_HEADER_SIZE];
    bhttp_encode_frame_header(hdr_bytes, &hdr);

    if (!bhttp_buf_append(out, hdr_bytes, BHTTP_HEADER_SIZE)) {
        bhttp_buf_free(&payload);
        return false;
    }
    if (!bhttp_buf_append(out, payload.data, payload.len)) {
        bhttp_buf_free(&payload);
        return false;
    }

    bhttp_buf_free(&payload);
    return true;
}

bool bhttp_decode_response_payload(const uint8_t *payload, size_t len, bhttp_response_t *resp) {
    if (!payload || !resp) return false;
    memset(resp, 0, sizeof(*resp));

    if (len < 2) return false;
    resp->status_code = ((uint16_t)payload[0] << 8) | (uint16_t)payload[1];
    size_t offset = 2;

    if (offset >= len) {
        /* No header count byte */
        return false;
    }

    if (!decode_headers(payload, len, &offset, resp->headers, &resp->header_count)) {
        bhttp_response_free(resp);
        return false;
    }

    /* Remainder of payload is body */
    size_t body_len = len - offset;
    resp->body_len = body_len;
    if (body_len > 0) {
        resp->body = (uint8_t *)malloc(body_len + 1);
        if (!resp->body) {
            bhttp_response_free(resp);
            return false;
        }
        memcpy(resp->body, payload + offset, body_len);
        resp->body[body_len] = '\0'; /* NULL-terminate safely for text */
    } else {
        resp->body = NULL;
    }

    return true;
}

void bhttp_response_free(bhttp_response_t *resp) {
    if (!resp) return;
    for (uint8_t i = 0; i < resp->header_count; i++) {
        if (resp->headers[i].custom_name) {
            free(resp->headers[i].custom_name);
            resp->headers[i].custom_name = NULL;
        }
        if (resp->headers[i].value) {
            free(resp->headers[i].value);
            resp->headers[i].value = NULL;
        }
    }
    if (resp->body) {
        free(resp->body);
        resp->body = NULL;
    }
    resp->header_count = 0;
    resp->body_len = 0;
}

/* ====================================================================
 * ROBUST SOCKET I/O
 * ==================================================================== */

ssize_t bhttp_read_exact(int fd, void *buf, size_t count) {
    size_t total = 0;
    uint8_t *ptr = (uint8_t *)buf;

    while (total < count) {
        ssize_t n = recv(fd, (char *)(ptr + total), (int)(count - total), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1; /* Socket error */
        }
        if (n == 0) {
            /* Connection closed by peer */
            if (total == 0) return 0; /* Clean EOF before frame */
            return -2; /* Unexpected EOF mid-frame (truncated) */
        }
        total += (size_t)n;
    }
    return (ssize_t)total;
}

ssize_t bhttp_write_all(int fd, const void *buf, size_t count) {
    size_t total = 0;
    const uint8_t *ptr = (const uint8_t *)buf;

    while (total < count) {
        ssize_t n = send(fd, (const char *)(ptr + total), (int)(count - total), 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) {
            return -1;
        }
        total += (size_t)n;
    }
    return (ssize_t)total;
}

bool bhttp_skip_bytes(int fd, size_t count) {
    uint8_t discard_buf[4096];
    size_t remaining = count;
    while (remaining > 0) {
        size_t to_read = remaining < sizeof(discard_buf) ? remaining : sizeof(discard_buf);
        ssize_t n = bhttp_read_exact(fd, discard_buf, to_read);
        if (n != (ssize_t)to_read) {
            return false;
        }
        remaining -= to_read;
    }
    return true;
}

/* ====================================================================
 * HEX DUMP & DEBUG FORMATTERS
 * ==================================================================== */

void bhttp_hexdump(FILE *stream, const char *title, const uint8_t *data, size_t len) {
    if (title) {
        fprintf(stream, "%s (%zu bytes):\n", title, len);
    }
    for (size_t i = 0; i < len; i += 16) {
        fprintf(stream, "  %04x  ", (unsigned int)i);
        for (size_t j = 0; j < 16; j++) {
            if (i + j < len) {
                fprintf(stream, "%02x ", data[i + j]);
            } else {
                fprintf(stream, "   ");
            }
            if (j == 7) fprintf(stream, " ");
        }
        fprintf(stream, " |");
        for (size_t j = 0; j < 16; j++) {
            if (i + j < len) {
                uint8_t c = data[i + j];
                fprintf(stream, "%c", isprint(c) ? c : '.');
            } else {
                fprintf(stream, " ");
            }
        }
        fprintf(stream, "|\n");
    }
}

void bhttp_dump_frame(FILE *stream, const bhttp_frame_header_t *hdr, const uint8_t *payload) {
    const char *type_str = "UNKNOWN";
    if (hdr->frame_type == BHTTP_FRAME_REQUEST) type_str = "REQUEST (0x01)";
    else if (hdr->frame_type == BHTTP_FRAME_RESPONSE) type_str = "RESPONSE (0x02)";
    else if (hdr->frame_type == BHTTP_FRAME_GOAWAY) type_str = "GOAWAY (0x03)";

    fprintf(stream, "--- BHTTP Frame Header (12 bytes) ---\n");
    fprintf(stream, "  Magic:          0x%04X ('%c%c')\n", hdr->magic, (char)(hdr->magic >> 8), (char)(hdr->magic & 0xFF));
    fprintf(stream, "  Version:        %u\n", hdr->version);
    fprintf(stream, "  Frame Type:     %s\n", type_str);
    fprintf(stream, "  Flags:          0x%02X%s\n", hdr->flags, (hdr->flags & BHTTP_FLAG_END_STREAM) ? " (END_STREAM)" : "");
    fprintf(stream, "  Reserved:       0x%02X%02X%02X\n", hdr->reserved[0], hdr->reserved[1], hdr->reserved[2]);
    fprintf(stream, "  Payload Length: %u bytes\n", hdr->payload_len);

    if (payload && hdr->payload_len > 0) {
        bhttp_hexdump(stream, "Payload", payload, hdr->payload_len);
    }
}
