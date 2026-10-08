#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#ifdef _WIN32
    #include <io.h>
    #include <fcntl.h>
    #pragma comment(lib, "ws2_32.lib")
#endif

typedef struct {
    char host[512];
    int  port;
    char path[1024];
} url_parts_t;

static bool parse_url(const char *url, url_parts_t *out) {
    if (!url || !out) return false;

    /* Skip optional http:// or bhttp:// scheme */
    const char *p = url;
    if (strncmp(p, "http://", 7) == 0) p += 7;
    else if (strncmp(p, "bhttp://", 8) == 0) p += 8;

    /* Extract host:port until '/' or '\0' */
    const char *slash = strchr(p, '/');
    char hostport[300];
    if (slash) {
        size_t hplen = (size_t)(slash - p);
        if (hplen >= sizeof(hostport)) return false;
        strncpy(hostport, p, hplen);
        hostport[hplen] = '\0';
        strncpy(out->path, slash, sizeof(out->path) - 1);
        out->path[sizeof(out->path) - 1] = '\0';
    } else {
        strncpy(hostport, p, sizeof(hostport) - 1);
        hostport[sizeof(hostport) - 1] = '\0';
        strcpy(out->path, "/");
    }

    if (out->path[0] == '\0') {
        strcpy(out->path, "/");
    }

    /* Parse host and optional :port */
    char *colon = strchr(hostport, ':');
    if (colon) {
        *colon = '\0';
        snprintf(out->host, sizeof(out->host), "%s", hostport);
        out->port = atoi(colon + 1);
        if (out->port <= 0 || out->port > 65535) return false;
    } else {
        snprintf(out->host, sizeof(out->host), "%s", hostport);
        out->port = 80; /* Default port */
    }

    return strlen(out->host) > 0;
}

static int connect_to_host(const char *host, int port) {
    char port_str[16];
    snprintf(port_str, sizeof(port_str), "%d", port);

    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET; /* IPv4 */
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int status = getaddrinfo(host, port_str, &hints, &res);
    if (status != 0) {
        fprintf(stderr, "bcurl error: getaddrinfo: %s\n", gai_strerror(status));
        return -1;
    }

    int sock = -1;
    for (struct addrinfo *p = res; p != NULL; p = p->ai_next) {
        sock = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (sock < 0) continue;

        if (connect(sock, p->ai_addr, p->ai_addrlen) == 0) {
            break; /* Successfully connected */
        }

#ifdef _WIN32
        closesocket(sock);
#else
        close(sock);
#endif
        sock = -1;
    }

    freeaddrinfo(res);
    return sock;
}

int main(int argc, char *argv[]) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif

    bool verbose = false;
    const char *url_arg = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = true;
        } else if (argv[i][0] != '-') {
            url_arg = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            fprintf(stderr, "Usage: %s [-v] <host:port/path>\n", argv[0]);
            return 1;
        }
    }

    if (!url_arg) {
        fprintf(stderr, "Usage: %s [-v] <host:port/path>\n", argv[0]);
        fprintf(stderr, "Example: %s -v localhost:9000/index.html\n", argv[0]);
        return 1;
    }

    url_parts_t target;
    if (!parse_url(url_arg, &target)) {
        fprintf(stderr, "bcurl error: Invalid URL format '%s'\n", url_arg);
        return 1;
    }

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "bcurl error: WSAStartup failed\n");
        return 1;
    }
#endif

    if (verbose) {
        fprintf(stderr, "* Connecting to %s:%d...\n", target.host, target.port);
        fprintf(stderr, "* Requested path: %s\n", target.path);
    }

    /* Single TCP connection */
    int sock = connect_to_host(target.host, target.port);
    if (sock < 0) {
        fprintf(stderr, "bcurl error: Failed to connect to %s:%d\n", target.host, target.port);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    if (verbose) {
        fprintf(stderr, "* Connected successfully (socket=%d)\n", sock);
    }

    /* Build Request Headers */
    bhttp_header_t req_headers[4];
    uint8_t req_hcount = 0;

    char host_header_val[530];
    snprintf(host_header_val, sizeof(host_header_val), "%s:%d", target.host, target.port);

    /* 1. Host */
    req_headers[req_hcount].id = BHTTP_HEADER_HOST;
    req_headers[req_hcount].custom_name = NULL;
    req_headers[req_hcount].value = host_header_val;
    req_hcount++;

    /* 2. User-Agent */
    req_headers[req_hcount].id = BHTTP_HEADER_USER_AGENT;
    req_headers[req_hcount].custom_name = NULL;
    req_headers[req_hcount].value = (char *)"bcurl/1.0 (BHTTP/1)";
    req_hcount++;

    /* 3. Accept */
    req_headers[req_hcount].id = BHTTP_HEADER_ACCEPT;
    req_headers[req_hcount].custom_name = NULL;
    req_headers[req_hcount].value = (char *)"*/*";
    req_hcount++;

    /* 4. Connection */
    req_headers[req_hcount].id = BHTTP_HEADER_CONNECTION;
    req_headers[req_hcount].custom_name = NULL;
    req_headers[req_hcount].value = (char *)"close";
    req_hcount++;

    /* Serialize Request Frame */
    bhttp_buf_t req_buf;
    bhttp_buf_init(&req_buf);

    if (!bhttp_encode_request(&req_buf, target.path, req_headers, req_hcount, BHTTP_FLAG_END_STREAM)) {
        fprintf(stderr, "bcurl error: Failed to encode request frame\n");
        bhttp_buf_free(&req_buf);
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return 1;
    }

    if (verbose) {
        fprintf(stderr, "> Sending BHTTP/1 Request Frame (%zu bytes total):\n", req_buf.len);
        bhttp_frame_header_t req_hdr;
        bhttp_decode_frame_header(req_buf.data, &req_hdr);
        bhttp_dump_frame(stderr, &req_hdr, req_buf.data + BHTTP_HEADER_SIZE);
    }

    /* Send request frame over socket */
    ssize_t sent = bhttp_write_all(sock, req_buf.data, req_buf.len);
    bhttp_buf_free(&req_buf);

    if (sent < 0) {
        fprintf(stderr, "bcurl error: Failed to send request frame\n");
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return 1;
    }

    /* Read response frame(s) from server */
    bhttp_frame_header_t resp_hdr;
    uint8_t *resp_payload = NULL;

    while (1) {
        uint8_t hdr_bytes[BHTTP_HEADER_SIZE];
        ssize_t rn = bhttp_read_exact(sock, hdr_bytes, BHTTP_HEADER_SIZE);
        if (rn == 0) {
            fprintf(stderr, "bcurl error: Server closed connection unexpectedly without response\n");
#ifdef _WIN32
            closesocket(sock);
            WSACleanup();
#else
            close(sock);
#endif
            return 1;
        }
        if (rn < 0) {
            fprintf(stderr, "bcurl error: Error reading frame header from server\n");
#ifdef _WIN32
            closesocket(sock);
            WSACleanup();
#else
            close(sock);
#endif
            return 1;
        }

        if (!bhttp_decode_frame_header(hdr_bytes, &resp_hdr)) {
            fprintf(stderr, "bcurl error: Failed to decode frame header from server\n");
#ifdef _WIN32
            closesocket(sock);
            WSACleanup();
#else
            close(sock);
#endif
            return 1;
        }

        if (resp_hdr.magic != BHTTP_MAGIC_U16) {
            fprintf(stderr, "bcurl error: Invalid magic from server (0x%04X)\n", resp_hdr.magic);
#ifdef _WIN32
            closesocket(sock);
            WSACleanup();
#else
            close(sock);
#endif
            return 1;
        }

        if (resp_hdr.payload_len > BHTTP_MAX_PAYLOAD_SIZE) {
            fprintf(stderr, "bcurl error: Server response payload too large (%u bytes)\n", resp_hdr.payload_len);
#ifdef _WIN32
            closesocket(sock);
            WSACleanup();
#else
            close(sock);
#endif
            return 1;
        }

        /* Unknown Frame Handling: skip cleanly */
        if (resp_hdr.frame_type != BHTTP_FRAME_RESPONSE) {
            if (verbose) {
                fprintf(stderr, "* Received frame type 0x%02X (not RESPONSE), skipping %u bytes...\n", resp_hdr.frame_type, resp_hdr.payload_len);
            }
            if (!bhttp_skip_bytes(sock, resp_hdr.payload_len)) {
                fprintf(stderr, "bcurl error: Failed to skip unexpected frame payload\n");
#ifdef _WIN32
                closesocket(sock);
                WSACleanup();
#else
                close(sock);
#endif
                return 1;
            }
            continue; /* Read next frame */
        }

        /* Response Frame arrived */
        if (resp_hdr.payload_len > 0) {
            resp_payload = (uint8_t *)malloc(resp_hdr.payload_len);
            if (!resp_payload) {
                fprintf(stderr, "bcurl error: Out of memory\n");
#ifdef _WIN32
                closesocket(sock);
                WSACleanup();
#else
                close(sock);
#endif
                return 1;
            }

            ssize_t pn = bhttp_read_exact(sock, resp_payload, resp_hdr.payload_len);
            if (pn != (ssize_t)resp_hdr.payload_len) {
                fprintf(stderr, "bcurl error: Truncated response payload from server\n");
                free(resp_payload);
#ifdef _WIN32
                closesocket(sock);
                WSACleanup();
#else
                close(sock);
#endif
                return 1;
            }
        }
        break;
    }

    if (verbose) {
        fprintf(stderr, "< Received BHTTP/1 Response Frame (%u payload bytes):\n", resp_hdr.payload_len);
        bhttp_dump_frame(stderr, &resp_hdr, resp_payload);
    }

    /* Decode Response Payload */
    bhttp_response_t resp;
    if (!bhttp_decode_response_payload(resp_payload, resp_hdr.payload_len, &resp)) {
        fprintf(stderr, "bcurl error: Failed to decode response payload\n");
        if (resp_payload) free(resp_payload);
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return 1;
    }

    if (verbose) {
        fprintf(stderr, "< HTTP Status: %u\n", resp.status_code);
        for (uint8_t i = 0; i < resp.header_count; i++) {
            const char *hname = (resp.headers[i].id != BHTTP_HEADER_CUSTOM)
                                ? bhttp_header_id_to_name(resp.headers[i].id)
                                : resp.headers[i].custom_name;
            fprintf(stderr, "< Header [%u]: %s: %s\n", resp.headers[i].id, hname ? hname : "custom", resp.headers[i].value ? resp.headers[i].value : "");
        }
        fprintf(stderr, "< Body Length: %zu bytes\n", resp.body_len);
    }

    /* Output ONLY the body to stdout */
    if (resp.body_len > 0 && resp.body) {
        fwrite(resp.body, 1, resp.body_len, stdout);
        fflush(stdout);
    }

    uint16_t status = resp.status_code;

    bhttp_response_free(&resp);
    if (resp_payload) free(resp_payload);

#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif

    /* Exit 0 on 2xx success, non-zero on 4xx/5xx */
    if (status >= 200 && status < 300) {
        return 0;
    } else {
        if (!verbose) {
            fprintf(stderr, "bcurl: Request failed with status %u\n", status);
        }
        return (status >= 400 && status <= 599) ? (status / 100) : 1;
    }
}
