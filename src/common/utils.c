#include "common/utils.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>

// Questo helper mi serve per parse robusti degli argomenti numerici: converto,
// controllo il range e restituisco anche un flag esplicito di validita.
long utils_parse_long(const char *s, long min_value, long max_value, int *ok)
{
    char *parse_end = NULL;
    long parsed_value;

    if (ok != NULL)
    {
        *ok = 0;
    }
    if (s == NULL || *s == '\0')
    {
        return 0;
    }
    errno = 0;
    parsed_value = strtol(s, &parse_end, 10);
    if (errno != 0 || parse_end == s || *parse_end != '\0' ||
        parsed_value < min_value || parsed_value > max_value)
    {
        return 0;
    }
    if (ok != NULL)
    {
        *ok = 1;
    }
    return parsed_value;
}
