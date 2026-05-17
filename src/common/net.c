#define _POSIX_C_SOURCE 200112L

#include "common/net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

int net_set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0)
    {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int net_send_all(int fd, const char *buf, size_t len)
{
    // Funzione di utilità per inviare tutti i byte di un buffer a un socket. Prende in input il file descriptor del socket, un buffer di dati e la sua lunghezza, e utilizza un ciclo per chiamare send finché non sono stati inviati tutti i byte. Gestisce correttamente gli errori di invio, come EINTR, e restituisce 0 in caso di successo o -1 in caso di errore.
    size_t sent = 0;

    while (sent < len)
    {
        ssize_t n = send(fd, buf + sent, len - sent, 0);
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return -1;
        }
        if (n == 0)
        {
            return -1;
        }
        sent += (size_t)n;
    }
    return 0;
}

int net_send_line(int fd, const char *line)
{
    // Funzione di utilità per inviare una linea di testo terminata da newline a un socket. Prende in input il file descriptor del socket e la stringa da inviare, e utilizza net_send_all per assicurarsi che l'intera stringa venga inviata. Restituisce 0 in caso di successo o -1 in caso di errore.
    return net_send_all(fd, line, strlen(line));
}
// Funzione per creare un socket server TCP che ascolta sulla porta specificata. Restituisce il file descriptor del socket in caso di successo o -1 in caso di errore
int net_create_server_socket(const char *port)
{
    struct sockaddr_in addr;
    char *endptr;
    long port_num;
    int fd = -1;
    int yes = 1;
    // Converto la porta da stringa a long e controllo che sia un numero valido compreso tra 1 e 65535
    errno = 0;
    port_num = strtol(port, &endptr, 10);
    if (errno != 0 || endptr == port || *endptr != '\0' ||
        port_num < 1 || port_num > 65535)
    {
        return -1;
    }
    // Creo un socket TCP IPv4
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
    {
        return -1;
    }
    // Imposto l'opzione SO_REUSEADDR per permettere al server di riavviarsi rapidamente senza dover aspettare che la porta venga liberata dal sistema operativo
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) != 0)
    {
        close(fd);
        return -1;
    }
    // Preparo la struttura sockaddr_in per il bind, impostando la famiglia di indirizzi a AF_INET, l'indirizzo IP a INADDR_ANY (tutte le interfacce) e la porta al numero specificato
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((uint16_t)port_num);
    // Eseguo il bind del socket all'indirizzo e alla porta specificati e inizio ad ascoltare le connessioni in arrivo con una coda di backlog di 16. Se si verifica un errore durante il bind o l'ascolto, chiudo il socket e restituisco -1
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0)
    {   
        close(fd);
        return -1;
    }
    if (listen(fd, 16) != 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

int net_connect_tcp(const char *host, const char *port)
{
    struct addrinfo hints;
    struct addrinfo *res = NULL;
    struct addrinfo *p;
    int fd = -1;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res) != 0)
    {
        return -1;
    }

    for (p = res; p != NULL; p = p->ai_next)
    {
        fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (fd < 0)
        {
            continue;
        }
        if (connect(fd, p->ai_addr, p->ai_addrlen) == 0)
        {
            break;
        }
        close(fd);
        fd = -1;
    }

    freeaddrinfo(res);
    return fd;
}
