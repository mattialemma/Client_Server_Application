#ifndef USERS_H
#define USERS_H

#include "common/protocol.h"

typedef struct {
    char nickname[NICK_MAX + 1];
    char password[PASS_MAX + 1];
} user_t;

typedef struct {
    user_t *items;
    size_t count;
    size_t capacity;
} user_db_t;

void users_free(user_db_t *db);
int users_register(user_db_t *db, const char *nickname, const char *password);
int users_authenticate(const user_db_t *db, const char *nickname, const char *password);

#endif
