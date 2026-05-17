#ifndef UTILS_H
#define UTILS_H

long utils_parse_long(const char *s, long min_value, long max_value, int *ok);
char utils_owner_symbol(const char *nickname);

#endif
