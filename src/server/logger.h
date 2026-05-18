#ifndef SERVER_LOGGER_H
#define SERVER_LOGGER_H

int server_log_init(const char *path);
void server_log_close(void);
void server_log_info(const char *fmt, ...);
void server_log_error(const char *fmt, ...);

#endif
