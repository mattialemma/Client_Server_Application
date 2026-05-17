#include "common/protocol.h"

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

int proto_valid_name(const char *s, size_t max_len) {
    // Verifica se una stringa è un nome valido secondo le regole del protocollo. Un nome valido deve essere non vuoto, non superare la lunghezza massima specificata e contenere solo caratteri alfanumerici, underscore o trattini. Restituisce 1 se il nome è valido, altrimenti 0.
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

int proto_parse_direction(const char *s, direction_t *dir) {
    // Converte una stringa in una direzione. Accetta "UP", "DOWN", "LEFT", "RIGHT" (case-insensitive) o le loro abbreviazioni "w", "s", "a", "d". Se la conversione ha successo, memorizza la direzione risultante in 'dir' e restituisce 0; altrimenti, restituisce -1.
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

const char *proto_direction_name(direction_t dir) {
    // Restituisce una stringa rappresentante la direzione specificata. Se la direzione è valida, restituisce "UP", "DOWN", "LEFT" o "RIGHT"; altrimenti, restituisce "UNKNOWN".
    switch (dir) {
        case DIR_UP: return "UP";
        case DIR_DOWN: return "DOWN";
        case DIR_LEFT: return "LEFT";
        case DIR_RIGHT: return "RIGHT";
        default: return "UNKNOWN";
    }
}

int proto_split(char *line, char **tokens, int max_tokens) {
    // Suddivide una linea di testo in token, utilizzando spazi, tabulazioni e ritorni a capo come delimitatori. Modifica la stringa originale inserendo terminatori null ('\0') dopo ogni token e memorizza i puntatori ai token nell'array 'tokens'. Restituisce il numero di token trovati, che sarà al massimo 'max_tokens'. Se la linea contiene più di 'max_tokens' token, i token in eccesso verranno ignorati.
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

int proto_make_line(char *dst, size_t dst_size, const char *fmt, ...) {
    // Funzione di utilità per costruire una linea di protocollo formattata. Prende in input un buffer di destinazione, la sua dimensione, una stringa di formato e un numero variabile di argomenti, utilizza vsnprintf per formattare la stringa e assicura che sia terminata da un newline. Se la formattazione ha successo e la stringa risultante si adatta nel buffer, restituisce il numero di byte scritti (escluso il terminatore null); altrimenti, restituisce -1.
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
