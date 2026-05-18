#ifndef SERVER_H
#define SERVER_H

#include "server/game.h"
#include "server/users.h"

#include <time.h>

typedef struct {
    int fd; 
    int authenticated; 
    int player_id;
    char inbuf[PROTO_MAX_LINE];
    size_t inbuf_len;
} client_session_t;

typedef struct {
    int listen_fd;
    int duration_sec;
    int period_sec;
    int running; 
    time_t start_time;
    time_t next_update;
    user_db_t users;
    game_t game;
    client_session_t *clients;
    size_t client_count;
    size_t client_capacity;
} server_t;

int server_run(const char *port, int duration_sec, int period_sec);

#endif
