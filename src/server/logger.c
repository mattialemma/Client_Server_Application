#include "server/logger.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static FILE *g_server_log = NULL;

static void server_log_write(const char *level, const char *fmt, va_list ap)
{
    time_t now;
    struct tm tm_now;
    char ts[32];

    if (g_server_log == NULL)
    {
        return;
    }

    now = time(NULL);
    if (localtime_r(&now, &tm_now) == NULL)
    {
        snprintf(ts, sizeof(ts), "1970-01-01 00:00:00");
    }
    else
    {
        strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", &tm_now);
    }

    fprintf(g_server_log, "[%s] %s ", ts, level);
    vfprintf(g_server_log, fmt, ap);
    fputc('\n', g_server_log);
    fflush(g_server_log);
}

int server_log_init(const char *path)
{
    if (path == NULL || path[0] == '\0')
    {
        path = "server.log";
    }
    g_server_log = fopen(path, "a");
    if (g_server_log == NULL)
    {
        return -1;
    }
    setvbuf(g_server_log, NULL, _IOLBF, 0);
    server_log_info("logging inizializzato su %s", path);
    return 0;
}

void server_log_close(void)
{
    if (g_server_log == NULL)
    {
        return;
    }
    server_log_info("chiusura logging");
    fclose(g_server_log);
    g_server_log = NULL;
}

void server_log_info(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    server_log_write("INFO", fmt, ap);
    va_end(ap);
}

void server_log_error(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    server_log_write("ERROR", fmt, ap);
    va_end(ap);
}
