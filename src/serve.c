#define _GNU_SOURCE
#include "protocol.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include <signal.h>
#include <errno.h>

#ifdef _WIN32
  #include <direct.h>
  #define realpath(N,R) _fullpath((R),(N),_MAX_PATH)
  #define PATH_SEP '\\'
#else
  #include <unistd.h>
  #define PATH_SEP '/'
#endif

/* Global shutdown flag */
static volatile sig_atomic_t g_running = 1;

static void handle_sigint(int sig) {
    (void)sig;
    g_running = 0;
}

/* Detect MIME Content-Type by file extension */
static const char *get_content_type(const char *path) {
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";

    if (strcasecmp(dot, ".html") == 0 || strcasecmp(dot, ".htm") == 0)
        return "text/html; charset=utf-8";
    if (strcasecmp(dot, ".txt") == 0)
        return "text/plain; charset=utf-8";
    if (strcasecmp(dot, ".css") == 0)
        return "text/css";
    if (strcasecmp(dot, ".js") == 0 || strcasecmp(dot, ".mjs") == 0)
        return "application/javascript";
    if (strcasecmp(dot, ".json") == 0)
        return "application/json";
    if (strcasecmp(dot, ".png") == 0)
        return "image/png";
    if (strcasecmp(dot, ".jpg") == 0 || strcasecmp(dot, ".jpeg") == 0)
        return "image/jpeg";
    if (strcasecmp(dot, ".svg") == 0)
        return "image/svg+xml";
    if (strcasecmp(dot, ".gif") == 0)
        return "image/gif";
    if (strcasecmp(dot, ".pdf") == 0)
        return "application/pdf";

    return "application/octet-stream";
}

/* Safe path resolution and validation against directory traversal */
static bool resolve_safe_path(const char *doc_root, const char *req_path, char *out_path, size_t out_size) {
    if (!doc_root || !req_path || !out_path || out_size == 0) return false;

    /* Normalize document root to canonical absolute path */
    char canonical_root[PATH_MAX];
    if (!realpath(doc_root, canonical_root)) {
        return false;
    }
    size_t root_len = strlen(canonical_root);

    /* Sanitize requested path: strip query string or fragments if present */
    char clean_path[PATH_MAX];
    size_t qlen = 0;
    while (req_path[qlen] && req_path[qlen] != '?' && req_path[qlen] != '#') {
        if (qlen + 1 >= sizeof(clean_path)) return false;
        clean_path[qlen] = req_path[qlen];
        qlen++;
    }
    clean_path[qlen] = '\0';

    /* Reject null bytes or unprintable chars */
    for (size_t i = 0; i < qlen; i++) {
        if ((unsigned char)clean_path[i] < 32 || (unsigned char)clean_path[i] == 127) {
            return false;
        }
    }

    /* Map "/" to "/index.html" if clean_path is empty or "/" */
    const char *target_subpath = clean_path;
    if (strcmp(clean_path, "") == 0 || strcmp(clean_path, "/") == 0) {
        target_subpath = "/index.html";
    }

    /* Build combined path */
    char combined[PATH_MAX * 2];
    if (target_subpath[0] == '/') {
        snprintf(combined, sizeof(combined), "%s%s", canonical_root, target_subpath);
    } else {
        snprintf(combined, sizeof(combined), "%s/%s", canonical_root, target_subpath);
    }

    /* Resolve canonical path of target */
    char canonical_target[PATH_MAX];
    if (!realpath(combined, canonical_target)) {
        /* File does not exist or invalid path components */
        return false;
    }

    /* Ensure canonical_target starts with canonical_root and a separator or exact match */
    if (strncmp(canonical_target, canonical_root, root_len) != 0) {
        /* Path traversal attempt escaped root! */
        return false;
    }
    if (canonical_target[root_len] != '\0' && canonical_target[root_len] != '/' && canonical_target[root_len] != '\\') {
        return false;
    }

    /* Check that it is a regular readable file, not a directory */
    struct stat st;
    if (stat(canonical_target, &st) != 0) {
        return false;
    }
    if (S_ISDIR(st.st_mode)) {
        /* If directory, try index.html inside it */
        char index_path[PATH_MAX * 2];
        int n = snprintf(index_path, sizeof(index_path), "%s/index.html", canonical_target);
        if (n < 0 || (size_t)n >= sizeof(index_path)) return false;

        char resolved_index[PATH_MAX];
        if (realpath(index_path, resolved_index)) {
            if (stat(resolved_index, &st) != 0 || S_ISDIR(st.st_mode)) {
                return false;
            }
            if (strlen(resolved_index) >= out_size) return false;
            snprintf(out_path, out_size, "%s", resolved_index);
            return true;
        } else {
            return false;
        }
    }

    if (strlen(canonical_target) >= out_size) return false;
    snprintf(out_path, out_size, "%s", canonical_target);
    return true;
}

/* Helper to send response frame */
static bool send_response_frame(int client_fd, uint16_t status_code, const char *content_type, const uint8_t *body, size_t body_len, uint8_t flags) {
    bhttp_header_t headers[4];
    uint8_t hcount = 0;

    char len_str[32];
    snprintf(len_str, sizeof(len_str), "%zu", body_len);

    /* Header 1: Content-Type */
    if (content_type) {
        headers[hcount].id = BHTTP_HEADER_CONTENT_TYPE;
        headers[hcount].custom_name = NULL;
        headers[hcount].value = (char *)content_type;
        hcount++;
    }

    /* Header 2: Content-Length */
    headers[hcount].id = BHTTP_HEADER_CONTENT_LENGTH;
    headers[hcount].custom_name = NULL;
    headers[hcount].value = len_str;
    hcount++;

    /* Header 3: Server */
    headers[hcount].id = BHTTP_HEADER_SERVER;
    headers[hcount].custom_name = NULL;
    headers[hcount].value = (char *)"BHTTP/1.0 Server";
    hcount++;

    /* Header 4: Connection */
    headers[hcount].id = BHTTP_HEADER_CONNECTION;
    headers[hcount].custom_name = NULL;
    headers[hcount].value = (char *)((flags & BHTTP_FLAG_END_STREAM) ? "close" : "keep-alive");
    hcount++;

    bhttp_buf_t out;
    bhttp_buf_init(&out);

    if (!bhttp_encode_response(&out, status_code, headers, hcount, body, body_len, flags)) {
        bhttp_buf_free(&out);
        return false;
    }

    size_t expected_len = out.len;
    ssize_t written = bhttp_write_all(client_fd, out.data, expected_len);
    bhttp_buf_free(&out);

    return (written == (ssize_t)expected_len);
}

static void send_error_response(int client_fd, uint16_t status_code, const char *msg, uint8_t flags) {
    char body[512];
    int len = snprintf(body, sizeof(body), "%u %s\n", status_code, msg ? msg : "Error");
    if (len < 0) len = 0;
    send_response_frame(client_fd, status_code, "text/plain; charset=utf-8", (const uint8_t *)body, (size_t)len, flags);
}

/* Process persistent TCP client connection */
static void handle_client(int client_fd, const char *doc_root) {
    printf("[Server] New client connection accepted (fd=%d)\n", client_fd);

    while (g_running) {
        uint8_t hdr_bytes[BHTTP_HEADER_SIZE];
        ssize_t n = bhttp_read_exact(client_fd, hdr_bytes, BHTTP_HEADER_SIZE);

        if (n == 0) {
            /* Client closed connection cleanly */
            printf("[Server] Client disconnected cleanly (fd=%d)\n", client_fd);
            break;
        }
        if (n < 0) {
            if (n == -2) {
                /* Incomplete frame header (truncated) */
                fprintf(stderr, "[Server] Incomplete frame header received from client\n");
                send_error_response(client_fd, BHTTP_STATUS_BAD_REQUEST, "Bad Request: Incomplete Header", BHTTP_FLAG_END_STREAM);
            }
            break;
        }

        bhttp_frame_header_t hdr;
        if (!bhttp_decode_frame_header(hdr_bytes, &hdr)) {
            send_error_response(client_fd, BHTTP_STATUS_BAD_REQUEST, "Bad Request: Frame Header Decoding Failed", BHTTP_FLAG_END_STREAM);
            break;
        }

        /* Check Magic */
        if (hdr.magic != BHTTP_MAGIC_U16) {
            fprintf(stderr, "[Server] Invalid magic 0x%04X (expected 0x4248)\n", hdr.magic);

            if (hdr.payload_len <= BHTTP_MAX_PAYLOAD_SIZE) {
                bhttp_skip_bytes(client_fd, hdr.payload_len);
            }

            send_error_response(client_fd, BHTTP_STATUS_BAD_REQUEST,
                        "Bad Request: Invalid Protocol Magic",
                        BHTTP_FLAG_END_STREAM);
            continue;
        }

        /* Check Version */
        if (hdr.version != BHTTP_VERSION_1) {
            fprintf(stderr, "[Server] Unsupported protocol version %u\n", hdr.version);

            if (hdr.payload_len <= BHTTP_MAX_PAYLOAD_SIZE) {
                bhttp_skip_bytes(client_fd, hdr.payload_len);
            }

            send_error_response(client_fd, BHTTP_STATUS_BAD_REQUEST,
                        "Bad Request: Unsupported Version",
                        BHTTP_FLAG_END_STREAM);
            continue;
        }

        /* Check Payload Length limits */
        if (hdr.payload_len > BHTTP_MAX_PAYLOAD_SIZE) {
            fprintf(stderr, "[Server] Payload length %u exceeds max limit %u\n", hdr.payload_len, BHTTP_MAX_PAYLOAD_SIZE);
            send_error_response(client_fd, BHTTP_STATUS_BAD_REQUEST, "Bad Request: Payload Too Large", BHTTP_FLAG_END_STREAM);
            break;
        }

        /* Unknown Frame Handling: MUST skip cleanly to maintain stream synchronization */
        if (hdr.frame_type != BHTTP_FRAME_REQUEST && hdr.frame_type != BHTTP_FRAME_GOAWAY) {
            printf("[Server] Unknown frame type 0x%02X encountered, skipping %u payload bytes...\n", hdr.frame_type, hdr.payload_len);
            if (!bhttp_skip_bytes(client_fd, hdr.payload_len)) {
                fprintf(stderr, "[Server] Failed to skip unknown frame payload (stream broken)\n");
                break;
            }
            printf("[Server] Cleanly skipped unknown frame type 0x%02X, ready for next frame\n", hdr.frame_type);
            continue;
        }

        if (hdr.frame_type == BHTTP_FRAME_GOAWAY) {
            printf("[Server] Received GOAWAY frame from client\n");
            bhttp_skip_bytes(client_fd, hdr.payload_len);
            break;
        }

        /* Frame Type is REQUEST (0x01) */
        uint8_t *payload = NULL;
        if (hdr.payload_len > 0) {
            payload = (uint8_t *)malloc(hdr.payload_len);
            if (!payload) {
                send_error_response(client_fd, BHTTP_STATUS_INTERNAL_ERROR, "Internal Server Error: Memory Allocation Failed", BHTTP_FLAG_END_STREAM);
                break;
            }

            ssize_t pn = bhttp_read_exact(client_fd, payload, hdr.payload_len);
            if (pn != (ssize_t)hdr.payload_len) {
                fprintf(stderr, "[Server] Truncated request payload received\n");
                free(payload);
                send_error_response(client_fd, BHTTP_STATUS_BAD_REQUEST, "Bad Request: Truncated Payload", BHTTP_FLAG_END_STREAM);
                break;
            }
        }

        bhttp_request_t req;
        if (!bhttp_decode_request_payload(payload, hdr.payload_len, &req)) {
            fprintf(stderr, "[Server] Malformed request payload\n");
            if (payload) free(payload);
            send_error_response(client_fd, BHTTP_STATUS_BAD_REQUEST, "Bad Request: Malformed Payload", 0);
            continue;
        }

        if (payload) free(payload);

        printf("[Server] Requested Path: \"%s\" (Headers: %u)\n", req.path, req.header_count);

        /* Resolve and validate target file path */
        char target_file[PATH_MAX];
        bool path_ok = resolve_safe_path(doc_root, req.path, target_file, sizeof(target_file));

        if (!path_ok) {
            /* Check if path traversal was attempted or simply 404 */
            if (strstr(req.path, "..") != NULL) {
                fprintf(stderr, "[Server] Path traversal attempt blocked: \"%s\"\n", req.path);
                send_error_response(client_fd, BHTTP_STATUS_BAD_REQUEST, "Bad Request: Forbidden Path Traversal", 0);
            } else {
                fprintf(stderr, "[Server] File not found: \"%s\"\n", req.path);
                send_error_response(client_fd, BHTTP_STATUS_NOT_FOUND, "404 Not Found", 0);
            }
            bhttp_request_free(&req);
            continue;
        }

        /* Read file contents */
        FILE *fp = fopen(target_file, "rb");
        if (!fp) {
            fprintf(stderr, "[Server] Failed to open file \"%s\": %s\n", target_file, strerror(errno));
            send_error_response(client_fd, BHTTP_STATUS_NOT_FOUND, "404 Not Found", 0);
            bhttp_request_free(&req);
            continue;
        }

        fseek(fp, 0, SEEK_END);
        long fsize = ftell(fp);
        fseek(fp, 0, SEEK_SET);

        if (fsize < 0 || (size_t)fsize > BHTTP_MAX_PAYLOAD_SIZE) {
            fclose(fp);
            send_error_response(client_fd, BHTTP_STATUS_INTERNAL_ERROR, "Internal Server Error: File Too Large", 0);
            bhttp_request_free(&req);
            continue;
        }

        uint8_t *file_data = NULL;
        if (fsize > 0) {
            file_data = (uint8_t *)malloc((size_t)fsize);
            if (!file_data) {
                fclose(fp);
                send_error_response(client_fd, BHTTP_STATUS_INTERNAL_ERROR, "Internal Server Error: Memory Exhausted", 0);
                bhttp_request_free(&req);
                continue;
            }
            size_t read_bytes = fread(file_data, 1, (size_t)fsize, fp);
            if (read_bytes != (size_t)fsize) {
                free(file_data);
                fclose(fp);
                send_error_response(client_fd, BHTTP_STATUS_INTERNAL_ERROR, "Internal Server Error: Read Failed", 0);
                bhttp_request_free(&req);
                continue;
            }
        }
        fclose(fp);

        const char *ctype = get_content_type(target_file);
        printf("[Server] Serving \"%s\" (%ld bytes, %s) -> 200 OK\n", target_file, fsize, ctype);

        uint8_t resp_flags = (hdr.flags & BHTTP_FLAG_END_STREAM) ? BHTTP_FLAG_END_STREAM : BHTTP_FLAG_NONE;
        send_response_frame(client_fd, BHTTP_STATUS_OK, ctype, file_data, (size_t)fsize, resp_flags);

        if (file_data) free(file_data);
        bhttp_request_free(&req);

        if (hdr.flags & BHTTP_FLAG_END_STREAM) {
            printf("[Server] END_STREAM flag set, closing connection\n");
            break;
        }
    }

#ifdef _WIN32
    closesocket(client_fd);
#else
    close(client_fd);
#endif
    printf("[Server] Closed connection fd=%d\n", client_fd);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <document-root> <port>\n", argv[0]);
        fprintf(stderr, "Example: %s ./www 9000\n", argv[0]);
        return 1;
    }

    const char *doc_root = argv[1];
    int port = atoi(argv[2]);
    if (port <= 0 || port > 65535) {
        fprintf(stderr, "Error: Invalid port number '%s'\n", argv[2]);
        return 1;
    }

    /* Verify document root exists */
    char canonical_root[PATH_MAX];
    if (!realpath(doc_root, canonical_root)) {
        fprintf(stderr, "Error: Document root '%s' does not exist or is inaccessible\n", doc_root);
        return 1;
    }

#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        fprintf(stderr, "Error: WSAStartup failed\n");
        return 1;
    }
#endif

    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);
#ifndef _WIN32
    signal(SIGPIPE, SIG_IGN);
#endif

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, (const char *)&opt, sizeof(opt));

    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons((uint16_t)port);

    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        perror("bind");
#ifdef _WIN32
        closesocket(server_fd);
#else
        close(server_fd);
#endif
        return 1;
    }

    if (listen(server_fd, 64) < 0) {
        perror("listen");
#ifdef _WIN32
        closesocket(server_fd);
#else
        close(server_fd);
#endif
        return 1;
    }

    printf("====================================================\n");
    printf("  BHTTP/1 Server Running\n");
    printf("  Document Root: %s\n", canonical_root);
    printf("  Listening on:  0.0.0.0:%d\n", port);
    printf("  Press Ctrl+C to stop\n");
    printf("====================================================\n");

    while (g_running) {
        struct sockaddr_in client_addr;
        socklen_t addrlen = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &addrlen);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            if (!g_running) break;
            perror("accept");
            continue;
        }

        handle_client(client_fd, canonical_root);
    }

    printf("\n[Server] Shutting down cleanly...\n");
#ifdef _WIN32
    closesocket(server_fd);
    WSACleanup();
#else
    close(server_fd);
#endif
    return 0;
}
