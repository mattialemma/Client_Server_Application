#include "common/utils.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

// Funzione per convertire una stringa in un long, con validazione dei limiti e gestione degli errori di conversione. Restituisce il valore convertito e imposta *ok a 1 se la conversione è riuscita, altrimenti 0.
long utils_parse_long(const char *s, long min_value, long max_value, int *ok) {
    char *end = NULL; //controllo su prima conversione
    long value;

    if (ok != NULL) {
        *ok = 0;
    }
    if (s == NULL || *s == '\0') {
        return 0;
    }
    errno = 0;
    value = strtol(s, &end, 10);
    if (errno != 0 || end == s || *end != '\0' || value < min_value || value > max_value) {
        return 0;
    }
    if (ok != NULL) {
        *ok = 1;
    }
    return value;
}

char utils_owner_symbol(const char *nickname) {
    unsigned char c;
    if (nickname == NULL || nickname[0] == '\0') {
        return '?';
    }
    c = (unsigned char)nickname[0];
    if (isalnum(c)) {
        return (char)toupper(c);
    }
    return '?';
}
