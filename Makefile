CC      = gcc
CFLAGS  = -Wall -Wextra -Wpedantic -O2 -std=c99 \
           -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE \
           -Iinclude
LDFLAGS =

SRC_DIR = src
INC_DIR = include
OBJ_DIR = obj
BIN_DIR = bin

PROTO_SRC = $(SRC_DIR)/protocol.c
SERVE_SRC = $(SRC_DIR)/serve.c
BCURL_SRC = $(SRC_DIR)/bcurl.c

PROTO_OBJ = $(OBJ_DIR)/protocol.o
SERVE_OBJ = $(OBJ_DIR)/serve.o
BCURL_OBJ = $(OBJ_DIR)/bcurl.o

SERVE_BIN = $(BIN_DIR)/serve
BCURL_BIN = $(BIN_DIR)/bcurl

# Root-level convenience symlinks (assignment commands: ./serve ./www 9000)
ROOT_SERVE  = serve
ROOT_BCURL  = bcurl

.PHONY: all clean test sanitize

all: dirs $(SERVE_BIN) $(BCURL_BIN) $(ROOT_SERVE) $(ROOT_BCURL)

dirs:
	mkdir -p $(OBJ_DIR) $(BIN_DIR)

# Object files
$(PROTO_OBJ): $(PROTO_SRC) $(INC_DIR)/protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

$(SERVE_OBJ): $(SERVE_SRC) $(INC_DIR)/protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

$(BCURL_OBJ): $(BCURL_SRC) $(INC_DIR)/protocol.h
	$(CC) $(CFLAGS) -c $< -o $@

# Binaries in bin/
$(SERVE_BIN): $(SERVE_OBJ) $(PROTO_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

$(BCURL_BIN): $(BCURL_OBJ) $(PROTO_OBJ)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

# Root-level symlinks so the assignment commands ./serve and ./bcurl work
$(ROOT_SERVE): $(SERVE_BIN)
	ln -sf $(SERVE_BIN) $(ROOT_SERVE)

$(ROOT_BCURL): $(BCURL_BIN)
	ln -sf $(BCURL_BIN) $(ROOT_BCURL)

# Development build with AddressSanitizer + UBSan
sanitize: CFLAGS += -fsanitize=address,undefined -g
sanitize: clean all

test: all
	python3 tests/test_suite.py

clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR) $(ROOT_SERVE) $(ROOT_BCURL)
