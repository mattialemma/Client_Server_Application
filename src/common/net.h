#ifndef NET_H
#define NET_H

#include <stddef.h>

int net_create_server_socket(const char *port);
int net_connect_tcp(const char *host, const char *port);
int net_send_line(int fd, const char *line);

#endif
