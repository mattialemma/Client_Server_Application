#define _POSIX_C_SOURCE 200112L

#include "common/net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

static char g_net_last_error[256] = "nessun errore di rete registrato";

static void net_set_error(const char *fmt, ...)
{
    // Tengo l'ultimo errore in una stringa globale semplice, cosi sia client
    // sia server possono stamparlo senza doversi portare dietro strutture extra.
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(g_net_last_error, sizeof(g_net_last_error), fmt, ap);
    va_end(ap);
}

// send() puo scrivere solo una parte dei byte richiesti: qui continuo finche
// non ho inviato tutto il buffer oppure incontro un errore reale.
static int net_send_all(int fd, const char *buffer, size_t buffer_length)
{
    size_t bytes_sent = 0;

    while (bytes_sent < buffer_length)
    {
        ssize_t sent_now = send(fd, buffer + bytes_sent, buffer_length - bytes_sent, 0);
        if (sent_now < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            net_set_error("send su fd=%d fallita: %s", fd, strerror(errno));
            return -1;
        }
        if (sent_now == 0)
        {
            net_set_error("send su fd=%d ha scritto 0 byte", fd);
            return -1;
        }
        bytes_sent += (size_t)sent_now;
    }
    return 0;
}

// Invia una riga gia formata sul socket.
int net_send_line(int fd, const char *line)
{
    return net_send_all(fd, line, strlen(line));
}

const char *net_last_error(void)
{
    return g_net_last_error;
}

// Qui preparo il socket di ascolto del server nel modo piu diretto possibile:
// valido la porta, creo il socket, abilito il riuso indirizzo e faccio bind+listen.
int net_create_server_socket(const char *port)
{
    struct sockaddr_in addr;
    char *endptr;
    long port_num;
    int fd = -1;
    int yes = 1;
    errno = 0;
    port_num = strtol(port, &endptr, 10);
    if (errno != 0 || endptr == port || *endptr != '\0' ||
        port_num < 1 || port_num > 65535)
    {
        net_set_error("porta non valida: %s", port);
        return -1;
    }
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        net_set_error("socket server fallita: %s", strerror(errno));
        return -1;
    }
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) != 0)
    {
        net_set_error("setsockopt(SO_REUSEADDR) fallita: %s", strerror(errno));
        close(fd);
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port_num);
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {
        net_set_error("bind sulla porta %s fallita: %s", port, strerror(errno));
        close(fd);
        return -1;
    }
    if (listen(fd, 16) != 0)
    {
        net_set_error("listen sulla porta %s fallita: %s", port, strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

// Lato client provo tutti gli endpoint che getaddrinfo() mi restituisce finche
// non trovo il primo a cui riesco davvero a collegarmi.
int net_connect_tcp(const char *host, const char *port)
{
    struct addrinfo hints;
    struct addrinfo *results = NULL;
    struct addrinfo *candidate;
    int fd = -1;
    int lookup_rc;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    lookup_rc = getaddrinfo(host, port, &hints, &results);
    if (lookup_rc != 0)
    {
        net_set_error("risoluzione indirizzo %s:%s fallita: %s", host, port, gai_strerror(lookup_rc));
        return -1;
    }

    for (candidate = results; candidate != NULL; candidate = candidate->ai_next)
    {
        fd = socket(candidate->ai_family, candidate->ai_socktype, candidate->ai_protocol);
        if (fd < 0)
        {
            net_set_error("socket client fallita per %s:%s: %s", host, port, strerror(errno));
            continue;
        }
        if (connect(fd, candidate->ai_addr, candidate->ai_addrlen) == 0)
        {
            break;
        }
        net_set_error("connect verso %s:%s fallita: %s", host, port, strerror(errno));
        close(fd);
        fd = -1;
    }

    freeaddrinfo(results);
    if (fd < 0 && strcmp(g_net_last_error, "nessun errore di rete registrato") == 0)
    {
        net_set_error("nessun endpoint raggiungibile per %s:%s", host, port);
    }
    return fd;
}
