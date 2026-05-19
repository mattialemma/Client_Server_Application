#include "common/protocol.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

// Qui tengo una validazione volutamente semplice: basta controllare che i dati
// siano non vuoti, abbastanza corti e composti solo da caratteri "sicuri".
int proto_valid_name(const char *s, size_t max_len) {
    size_t i;

    if (s == NULL || s[0] == '\0') {
        return 0;
    }
    for (i = 0; s[i] != '\0'; ++i) {
        if (i >= max_len) {
            return 0;
        }
        if (!isalnum((unsigned char)s[i]) && s[i] != '_' && s[i] != '-') {
            return 0;
        }
    }
    return 1;
}

// Questo helper mi permette di accettare sia il testo usato nel protocollo sia
// le scorciatoie da tastiera del client, ma di lavorare internamente sempre con un enum.
int proto_parse_direction(const char *s, direction_t *dir) {
    // Accetta le forme usate dal protocollo e i tasti rapidi del client.
    if (s == NULL || dir == NULL) {
        return -1;
    }
    if (strcmp(s, "UP") == 0 || strcmp(s, "up") == 0 || strcmp(s, "w") == 0) {
        *dir = DIR_UP;
        return 0;
    }
    if (strcmp(s, "DOWN") == 0 || strcmp(s, "down") == 0 || strcmp(s, "s") == 0) {
        *dir = DIR_DOWN;
        return 0;
    }
    if (strcmp(s, "LEFT") == 0 || strcmp(s, "left") == 0 || strcmp(s, "a") == 0) {
        *dir = DIR_LEFT;
        return 0;
    }
    if (strcmp(s, "RIGHT") == 0 || strcmp(s, "right") == 0 || strcmp(s, "d") == 0) {
        *dir = DIR_RIGHT;
        return 0;
    }
    return -1;
}

// Restituisce il nome canonico usato nel protocollo per una direzione.
const char *proto_direction_name(direction_t dir) {
    switch (dir) {
        case DIR_UP: return "UP";
        case DIR_DOWN: return "DOWN";
        case DIR_LEFT: return "LEFT";
        case DIR_RIGHT: return "RIGHT";
        default: return "UNKNOWN";
    }
}

// Tokenizzo la riga "in place": invece di allocare nuove stringhe, sostituisco
// gli spazi con '\0' e salvo i puntatori all'interno dello stesso buffer.
int proto_split(char *line, char **tokens, int max_tokens) {
    int count = 0;
    char *p = line;

    while (*p != '\0' && count < max_tokens) {
        while (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') {
            ++p;
        }
        if (*p == '\0') {
            break;
        }
        tokens[count++] = p;
        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
            ++p;
        }
        if (*p != '\0') {
            *p++ = '\0';
        }
    }
    return count;
}

// Tutti i messaggi applicativi devono terminare con newline; centralizzo qui
// questa regola per evitare dimenticanze nei vari punti del programma.
int proto_make_line(char *dst, size_t dst_size, const char *fmt, ...) {
    va_list ap;
    int n;

    if (dst == NULL || dst_size == 0 || fmt == NULL) {
        return -1;
    }

    va_start(ap, fmt);
    n = vsnprintf(dst, dst_size, fmt, ap);
    va_end(ap);

    if (n < 0 || (size_t)n + 2 > dst_size) {
        return -1;
    }
    dst[n] = '\n';
    dst[n + 1] = '\0';
    return n + 1;
}
