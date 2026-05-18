CC := gcc
CFLAGS := -std=c11 -D_POSIX_C_SOURCE=200112L -Wall -Wextra -Wpedantic -O2 -g -I src -MMD -MP
LDFLAGS :=

BIN_DIR := bin
COMMON_OBJS := build/common/net.o build/common/protocol.o build/common/utils.o
SERVER_OBJS := build/server/main.o build/server/server.o build/server/game.o build/server/users.o build/server/logger.o
CLIENT_OBJS := build/client/main.o build/client/client.o build/client/ui.o
ALL_OBJS := $(COMMON_OBJS) $(SERVER_OBJS) $(CLIENT_OBJS)
DEPS := $(ALL_OBJS:.o=.d)

.PHONY: all server client clean run-server run-client

all: server client

server: $(BIN_DIR)/server

client: $(BIN_DIR)/client

$(BIN_DIR)/server: $(COMMON_OBJS) $(SERVER_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(BIN_DIR)/client: $(COMMON_OBJS) $(CLIENT_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

run-server: server
	./$(BIN_DIR)/server 4242 300 5

run-client: client
	./$(BIN_DIR)/client 127.0.0.1 4242

clean:
	rm -rf build $(BIN_DIR)

-include $(DEPS)
