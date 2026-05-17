#include "server/users.h"

#include <stdlib.h>
#include <string.h>

#define INITIAL_USERS_CAPACITY 16

void users_free(user_db_t *db) {
    // Libera tutte le risorse allocate dal database degli utenti, inclusi gli array dinamici e i record degli utenti. Dopo aver liberato le risorse, azzera la struttura del database per evitare riferimenti a memoria già liberata. Questa funzione viene chiamata alla terminazione del server per assicurarsi che tutte le risorse del database degli utenti vengano correttamente liberate.
    free(db->items);
    memset(db, 0, sizeof(*db));
}

static int users_reserve(user_db_t *db, size_t needed) {
    // Funzione di utilità per assicurarsi che l'array degli utenti del database abbia una capacità sufficiente per memorizzare un certo numero di utenti. Se la capacità attuale è inferiore a quella necessaria, tenta di espandere l'array raddoppiando la sua dimensione fino a raggiungere o superare la capacità richiesta. Se l'espansione ha successo, restituisce 0; altrimenti, restituisce -1 in caso di errore di allocazione.
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

int users_exists(const user_db_t *db, const char *nickname) {
    // Funzione per verificare se un utente con un certo nickname esiste già nel database. Prende in input il database degli utenti e un nickname, e restituisce 1 se esiste un utente con quel nickname, o 0 altrimenti. La funzione scorre l'array degli utenti registrati confrontando i nickname fino a trovare una corrispondenza o raggiungere la fine dell'array.
    size_t i;
    for (i = 0; i < db->count; ++i) {
        if (strcmp(db->items[i].nickname, nickname) == 0) {
            return 1;
        }
    }
    return 0;
}

int users_register(user_db_t *db, const char *nickname, const char *password) {
    // Funzione per registrare un nuovo utente nel database. Prende in input il database degli utenti, un nickname e una password, e restituisce 0 in caso di successo, -1 se l'utente esiste già, -2 se i dati non sono validi (ad esempio, se il nickname o la password non rispettano i requisiti di formato), o -3 se il database è pieno e non può accettare nuovi utenti. Se la registrazione ha successo, aggiunge un nuovo record al database con il nickname e la password specificati.
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

int users_authenticate(const user_db_t *db, const char *nickname, const char *password) {
    // Funzione per autenticare un utente con un certo nickname e password. Prende in input il database degli utenti, un nickname e una password, e restituisce 0 se esiste un utente con quel nickname e password, o -1 altrimenti. La funzione scorre l'array degli utenti registrati confrontando i nickname e le password fino a trovare una corrispondenza o raggiungere la fine dell'array.
    size_t i;
    for (i = 0; i < db->count; ++i) {
        if (strcmp(db->items[i].nickname, nickname) == 0 &&
            strcmp(db->items[i].password, password) == 0) {
            return 0;
        }
    }
    return -1;
}
