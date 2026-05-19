#include "server/users.h"

#include <stdlib.h>
#include <string.h>

#define INITIAL_USERS_CAPACITY 16

// Il database utenti e solo un array dinamico in memoria: niente persistenza,
// quindi alla chiusura mi basta liberare l'array e azzerare la struttura.
void users_free(user_db_t *db) {
    free(db->items);
    memset(db, 0, sizeof(*db));
}

// Espando il database raddoppiando la capacita quando serve, cosi mantengo
// una logica semplice e un numero contenuto di realloc.
static int users_reserve(user_db_t *db, size_t needed) {
    user_t *new_items;
    size_t new_capacity = db->capacity == 0 ? INITIAL_USERS_CAPACITY : db->capacity;

    while (new_capacity < needed) {
        new_capacity *= 2;
    }
    if (new_capacity == db->capacity) {
        return 0;
    }
    new_items = realloc(db->items, new_capacity * sizeof(*new_items));
    if (new_items == NULL) {
        return -1;
    }
    db->items = new_items;
    db->capacity = new_capacity;
    return 0;
}

// Controlla se un nickname e gia registrato.
static int users_exists(const user_db_t *db, const char *nickname) {
    size_t i;
    for (i = 0; i < db->count; ++i) {
        if (strcmp(db->items[i].nickname, nickname) == 0) {
            return 1;
        }
    }
    return 0;
}

// Qui la registrazione e minimale: valido nickname/password, controllo che il
// nickname non esista gia e poi salvo tutto nel database in memoria.
int users_register(user_db_t *db, const char *nickname, const char *password) {
    if (!proto_valid_name(nickname, NICK_MAX) || !proto_valid_name(password, PASS_MAX)) {
        return -2;
    }
    if (users_exists(db, nickname)) {
        return -1;
    }
    if (users_reserve(db, db->count + 1) != 0) {
        return -3;
    }
    strncpy(db->items[db->count].nickname, nickname, NICK_MAX);
    strncpy(db->items[db->count].password, password, PASS_MAX);
    db->items[db->count].nickname[NICK_MAX] = '\0';
    db->items[db->count].password[PASS_MAX] = '\0';
    db->count++;
    return 0;
}

// L'autenticazione non fa hashing o accessi esterni: per questo progetto basta
// un confronto diretto sulle credenziali salvate in memoria.
int users_authenticate(const user_db_t *db, const char *nickname, const char *password) {
    size_t i;
    for (i = 0; i < db->count; ++i) {
        if (strcmp(db->items[i].nickname, nickname) == 0 &&
            strcmp(db->items[i].password, password) == 0) {
            return 0;
        }
    }
    return -1;
}
