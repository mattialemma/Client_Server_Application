#ifndef USERS_H
#define USERS_H

#include "common/protocol.h"
// Struttura per rappresentare un utente registrato, contenente il nickname e la password
typedef struct {
    char nickname[NICK_MAX + 1];
    char password[PASS_MAX + 1];
} user_t;
// Struttura per rappresentare un database di utenti registrati, contenente un array dinamico di utenti, il numero di utenti attualmente registrati e la capacità dell'array
typedef struct {
    user_t *items; // Array dinamico di utenti registrati
    size_t count;
    size_t capacity;
} user_db_t;

void users_free(user_db_t *db);
int users_register(user_db_t *db, const char *nickname, const char *password);
int users_authenticate(const user_db_t *db, const char *nickname, const char *password);
int users_exists(const user_db_t *db, const char *nickname);

#endif
