#include "server/server.h"

#include "common/net.h"
#include "common/protocol.h"
#include "common/utils.h"
#include "server/logger.h"

#include <errno.h>
#include <netdb.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>

#define INITIAL_CLIENTS_CAPACITY 16

// Riporta una sessione client allo stato libero.
static void client_reset(client_session_t *c)
{
    c->fd = -1;
    c->authenticated = 0;
    c->player_id = -1;
    c->remote_addr[0] = '\0';
    c->nickname[0] = '\0';
    c->inbuf_len = 0;
}

// Inizializza lo stato globale del server e la partita.
static void server_init(server_t *s, int listen_fd, int duration_sec, int period_sec)
{
    memset(s, 0, sizeof(*s));
    s->listen_fd = listen_fd;
    s->duration_sec = duration_sec;
    s->period_sec = period_sec;
    s->running = 1;
    s->start_time = time(NULL);
    s->next_update = s->start_time + period_sec;
    game_init(&s->game);
}

// Libera array dinamici e sottostrutture possedute dal server.
static void server_free(server_t *s)
{
    free(s->clients);
    users_free(&s->users);
    game_free(&s->game);
    s->clients = NULL;
    s->client_count = 0;
    s->client_capacity = 0;
}

// Garantisce spazio per almeno needed sessioni client.
static int server_reserve_clients(server_t *s, size_t needed)
{
    // Le sessioni crescono dinamicamente; il limite pratico resta quello di select/OS.
    client_session_t *new_clients;
    size_t i;
    size_t old_capacity = s->client_capacity;
    size_t new_capacity = old_capacity == 0 ? INITIAL_CLIENTS_CAPACITY : old_capacity;

    while (new_capacity < needed)
    {
        new_capacity *= 2;
    }
    if (new_capacity == old_capacity)
    {
        return 0;
    }
    new_clients = realloc(s->clients, new_capacity * sizeof(*new_clients));
    if (new_clients == NULL)
    {
        return -1;
    }
    s->clients = new_clients;
    for (i = old_capacity; i < new_capacity; ++i)
    {
        client_reset(&s->clients[i]);
    }
    s->client_capacity = new_capacity;
    return 0;
}

static void disconnect_client(server_t *s, int index);

// Costruisce una risposta S2C e la invia come singola riga di protocollo.
static int sendf(client_session_t *c, const char *fmt, ...)
{
    char payload[PROTO_MAX_LINE];
    char line[PROTO_MAX_LINE];
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(payload, sizeof(payload), fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof(payload))
    {
        return -1;
    }
    if (proto_make_line(line, sizeof(line), "%s", payload) < 0)
    {
        server_log_error("impossibile formare risposta per fd=%d", c->fd);
        return -1;
    }
    if (net_send_line(c->fd, line) != 0)
    {
        server_log_error("invio verso fd=%d (%s) fallito: %s",
                         c->fd,
                         c->remote_addr[0] != '\0' ? c->remote_addr : "-",
                         net_last_error());
        return -1;
    }
    server_log_info("risposta inviata fd=%d addr=%s nickname=%s payload=\"%s\"",
                    c->fd,
                    c->remote_addr[0] != '\0' ? c->remote_addr : "-",
                    c->nickname[0] != '\0' ? c->nickname : "-",
                    payload);
    return 0;
}

// Invia al client la mappa locale del suo giocatore.
static int send_local(server_t *s, client_session_t *c)
{
    char map[PROTO_MAX_LINE];
    if (c->authenticated && c->player_id >= 0)
    {
        if (game_build_local_map(&s->game, c->player_id, map, sizeof(map)) != 0)
        {
            return sendf(c, "S2C_ERR ENCODING_FAILED");
        }
        server_log_info("mappa locale generata nickname=%s player_id=%d", c->nickname, c->player_id);
        return sendf(c, "S2C_LOCAL_MAP %d %d %s", LOCAL_VIEW_W, LOCAL_VIEW_H, map);
    }
    return 0;
}

// Invia a un client la vista globale pubblica dello stato di gioco.
static int send_global_to_client(server_t *s, client_session_t *c)
{
    char map[PROTO_MAX_LINE];
    char pos[PROTO_MAX_LINE];
    if (c->fd >= 0 && c->authenticated)
    {
        if (game_build_global_map(&s->game, map, sizeof(map)) != 0 ||
            game_build_positions(&s->game, pos, sizeof(pos)) != 0)
        {
            return sendf(c, "S2C_ERR ENCODING_FAILED");
        }
        server_log_info("mappa globale generata per nickname=%s", c->nickname);
        return sendf(c, "S2C_GLOBAL_UPDATE %d %d %s %s", MAP_W, MAP_H, map, pos);
    }
    return 0;
}

// Invia l'aggiornamento globale a tutti i client autenticati.
static void broadcast_global(server_t *s)
{
    size_t i;
    size_t delivered = 0;

    for (i = 0; i < s->client_count; ++i)
    {
        if (send_global_to_client(s, &s->clients[i]) != 0)
        {
            disconnect_client(s, i);
        }
        else if (s->clients[i].fd >= 0 && s->clients[i].authenticated)
        {
            delivered++;
        }
    }
    server_log_info("broadcast globale completato client_autenticati=%zu", delivered);
}

// Chiude una connessione e aggiorna lo stato del giocatore associato.
static void disconnect_client(server_t *s, int index)
{
    client_session_t *c = &s->clients[index];
    char nickname[NICK_MAX + 1];
    char remote_addr[64];
    int fd = c->fd;

    snprintf(nickname, sizeof(nickname), "%s", c->nickname[0] != '\0' ? c->nickname : "-");
    snprintf(remote_addr, sizeof(remote_addr), "%s", c->remote_addr[0] != '\0' ? c->remote_addr : "-");
    if (c->fd >= 0)
    {
        close(c->fd);
    }
    if (c->authenticated && c->player_id >= 0)
    {
        // Il giocatore diventa offline, ma i territori restano assegnati al suo slot.
        game_remove_player(&s->game, c->player_id);
    }
    server_log_info("client disconnesso fd=%d addr=%s nickname=%s", fd, remote_addr, nickname);
    client_reset(c);
}

// Accetta una nuova connessione e la inserisce in uno slot disponibile.
static void accept_client(server_t *s)
{
    int fd;
    struct sockaddr_storage addr;
    socklen_t addr_len = sizeof(addr);
    char host[64];
    char service[16];
    char remote_addr[64];
    size_t i;
    size_t slot;

    fd = accept(s->listen_fd, (struct sockaddr *)&addr, &addr_len);
    if (fd < 0)
    {
        server_log_error("accept fallita: %s", strerror(errno));
        return;
    }
    if (getnameinfo((struct sockaddr *)&addr, addr_len,
                    host, sizeof(host),
                    service, sizeof(service),
                    NI_NUMERICHOST | NI_NUMERICSERV) == 0)
    {
        snprintf(remote_addr, sizeof(remote_addr), "%s:%s", host, service);
    }
    else
    {
        snprintf(remote_addr, sizeof(remote_addr), "sconosciuto");
    }
    // select(2) usa fd_set: descrittori oltre FD_SETSIZE non sono gestibili.
    if (fd >= FD_SETSIZE)
    {
        net_send_line(fd, "S2C_ERR SERVER_FULL\n");
        server_log_error("rifiutata connessione da %s: fd=%d oltre FD_SETSIZE", remote_addr, fd);
        close(fd);
        return;
    }
    for (i = 0; i < s->client_count; ++i)
    {
        if (s->clients[i].fd < 0)
        {
            client_reset(&s->clients[i]);
            s->clients[i].fd = fd;
            snprintf(s->clients[i].remote_addr, sizeof(s->clients[i].remote_addr), "%s", remote_addr);
            server_log_info("connessione accettata fd=%d addr=%s slot=%zu", fd, remote_addr, i);
            sendf(&s->clients[i], "S2C_OK CONNECTED");
            return;
        }
    }
    slot = s->client_count;
    if (server_reserve_clients(s, s->client_count + 1) != 0)
    {
        net_send_line(fd, "S2C_ERR SERVER_FULL\n");
        server_log_error("memoria insufficiente, connessione rifiutata da %s", remote_addr);
        close(fd);
        return;
    }
    s->client_count++;
    client_reset(&s->clients[slot]);
    s->clients[slot].fd = fd;
    snprintf(s->clients[slot].remote_addr, sizeof(s->clients[slot].remote_addr), "%s", remote_addr);
    server_log_info("connessione accettata fd=%d addr=%s slot=%zu", fd, remote_addr, slot);
    sendf(&s->clients[slot], "S2C_OK CONNECTED");
}

// Controlla che il comando richiedente appartenga a un client autenticato.
static int require_auth(client_session_t *c)
{
    if (!c->authenticated)
    {
        server_log_error("richiesta non autenticata fd=%d addr=%s", c->fd, c->remote_addr);
        sendf(c, "S2C_ERR NOT_AUTHENTICATED");
        return 0;
    }
    return 1;
}

// Gestisce C2S_REGISTER.
static void handle_register(server_t *s, client_session_t *c, char **tok, int ntok)
{
    int rc;
    if (ntok != 3)
    {
        server_log_error("register con sintassi non valida addr=%s", c->remote_addr);
        sendf(c, "S2C_ERR BAD_SYNTAX");
        return;
    }
    rc = users_register(&s->users, tok[1], tok[2]);
    if (rc == 0)
    {
        server_log_info("registrazione completata nickname=%s addr=%s", tok[1], c->remote_addr);
        sendf(c, "S2C_OK REGISTERED");
    }
    else if (rc == -1)
    {
        server_log_error("registrazione duplicata nickname=%s addr=%s", tok[1], c->remote_addr);
        sendf(c, "S2C_ERR USER_EXISTS");
    }
    else if (rc == -2)
    {
        server_log_error("registrazione non valida nickname=%s addr=%s", tok[1], c->remote_addr);
        sendf(c, "S2C_ERR INVALID_CREDENTIALS");
    }
    else
    {
        server_log_error("database utenti pieno per nickname=%s addr=%s", tok[1], c->remote_addr);
        sendf(c, "S2C_ERR USER_DB_FULL");
    }
}

// Gestisce C2S_LOGIN e crea la presenza del giocatore nella partita.
static void handle_login(server_t *s, client_session_t *c, char **tok, int ntok)
{
    int player_id;
    if (ntok != 3)
    {
        server_log_error("login con sintassi non valida addr=%s", c->remote_addr);
        sendf(c, "S2C_ERR BAD_SYNTAX");
        return;
    }
    if (c->authenticated)
    {
        server_log_error("login duplicato nickname=%s addr=%s", tok[1], c->remote_addr);
        sendf(c, "S2C_ERR ALREADY_AUTHENTICATED");
        return;
    }
    if (users_authenticate(&s->users, tok[1], tok[2]) != 0)
    {
        server_log_error("login fallito nickname=%s addr=%s", tok[1], c->remote_addr);
        sendf(c, "S2C_ERR AUTH_FAILED");
        return;
    }
    if (game_find_player(&s->game, tok[1]) >= 0)
    {
        server_log_error("utente gia online nickname=%s addr=%s", tok[1], c->remote_addr);
        sendf(c, "S2C_ERR USER_ALREADY_ONLINE");
        return;
    }
    player_id = game_add_player(&s->game, tok[1]);
    if (player_id < 0)
    {
        server_log_error("partita piena per nickname=%s addr=%s", tok[1], c->remote_addr);
        sendf(c, "S2C_ERR GAME_FULL");
        return;
    }
    c->authenticated = 1;
    c->player_id = player_id;
    strncpy(c->nickname, tok[1], NICK_MAX);
    c->nickname[NICK_MAX] = '\0';
    server_log_info("login riuscito nickname=%s player_id=%s posizione=(%d,%d) addr=%s",
                    c->nickname,
                    s->game.players[player_id].symbol,
                    s->game.players[player_id].x,
                    s->game.players[player_id].y,
                    c->remote_addr);
    sendf(c, "S2C_OK LOGGED_IN %s %s %d %d",
          c->nickname,
          s->game.players[player_id].symbol,
          s->game.players[player_id].x,
          s->game.players[player_id].y);
    send_local(s, c);
    broadcast_global(s);
}

// Gestisce C2S_MOVE e comunica l'esito del movimento.
static void handle_move(server_t *s, client_session_t *c, char **tok, int ntok)
{
    direction_t dir;
    int rc;
    if (!require_auth(c))
    {
        return;
    }
    if (ntok != 2 || proto_parse_direction(tok[1], &dir) != 0)
    {
        server_log_error("direzione non valida nickname=%s addr=%s", c->nickname, c->remote_addr);
        sendf(c, "S2C_ERR BAD_DIRECTION");
        return;
    }
    rc = game_move(&s->game, c->player_id, dir);
    if (rc == 0)
    {
        server_log_info("movimento riuscito nickname=%s posizione=(%d,%d)",
                        c->nickname,
                        s->game.players[c->player_id].x,
                        s->game.players[c->player_id].y);
        sendf(c, "S2C_OK MOVED %d %d", s->game.players[c->player_id].x, s->game.players[c->player_id].y);
    }
    else if (rc == -2)
    {
        server_log_error("movimento fuori mappa nickname=%s", c->nickname);
        sendf(c, "S2C_ERR OUT_OF_BOUNDS");
    }
    else if (rc == -3)
    {
        server_log_error("movimento contro muro nickname=%s", c->nickname);
        sendf(c, "S2C_ERR WALL");
    }
    else if (rc == -4)
    {
        server_log_error("movimento su cella occupata nickname=%s", c->nickname);
        sendf(c, "S2C_ERR OCCUPIED");
    }
    else
    {
        server_log_error("movimento fallito nickname=%s", c->nickname);
        sendf(c, "S2C_ERR MOVE_FAILED");
    }
    send_local(s, c);
}

// Gestisce C2S_LIST_USERS.
static void handle_users(server_t *s, client_session_t *c)
{
    char pos[PROTO_MAX_LINE];
    if (!require_auth(c))
    {
        return;
    }
    if (game_build_positions(&s->game, pos, sizeof(pos)) != 0)
    {
        sendf(c, "S2C_ERR ENCODING_FAILED");
        return;
    }
    server_log_info("lista utenti generata per nickname=%s", c->nickname);
    sendf(c, "S2C_USERS %s", pos);
}

// Smista una riga C2S verso il gestore del comando corrispondente.
static void handle_line(server_t *s, int index, char *line)
{
    client_session_t *c = &s->clients[index];
    char *tok[PROTO_MAX_TOKENS];
    int ntok = proto_split(line, tok, PROTO_MAX_TOKENS);

    if (ntok == 0)
    {
        return;
    }
    server_log_info("richiesta ricevuta fd=%d addr=%s nickname=%s payload=\"%s\"",
                    c->fd,
                    c->remote_addr[0] != '\0' ? c->remote_addr : "-",
                    c->nickname[0] != '\0' ? c->nickname : "-",
                    line);
    if (strcmp(tok[0], "C2S_REGISTER") == 0)
    {
        handle_register(s, c, tok, ntok);
    }
    else if (strcmp(tok[0], "C2S_LOGIN") == 0)
    {
        handle_login(s, c, tok, ntok);
    }
    else if (strcmp(tok[0], "C2S_MOVE") == 0)
    {
        handle_move(s, c, tok, ntok);
    }
    else if (strcmp(tok[0], "C2S_LIST_USERS") == 0 && ntok == 1)
    {
        handle_users(s, c);
    }
    else if (strcmp(tok[0], "C2S_LOCAL_MAP") == 0 && ntok == 1)
    {
        if (require_auth(c))
        {
            send_local(s, c);
        }
    }
    else if (strcmp(tok[0], "C2S_GLOBAL_MAP") == 0 && ntok == 1)
    {
        if (require_auth(c))
        {
            if (send_global_to_client(s, c) != 0)
            {
                disconnect_client(s, index);
            }
        }
    }
    else if (strcmp(tok[0], "C2S_QUIT") == 0 && ntok == 1)
    {
        server_log_info("richiesta uscita nickname=%s addr=%s", c->nickname, c->remote_addr);
        sendf(c, "S2C_OK BYE");
        disconnect_client(s, index);
    }
    else if (strncmp(tok[0], "C2S_", 4) == 0)
    {
        server_log_error("richiesta C2S con sintassi non valida payload=\"%s\"", line);
        sendf(c, "S2C_ERR BAD_SYNTAX");
    }
    else
    {
        server_log_error("comando sconosciuto payload=\"%s\"", line);
        sendf(c, "S2C_ERR UNKNOWN_COMMAND");
    }
}

// Legge dal socket client e processa tutte le righe complete ricevute.
static int read_client(server_t *s, int index)
{
    client_session_t *c = &s->clients[index];
    char tmp[512];
    ssize_t n;

    n = recv(c->fd, tmp, sizeof(tmp), 0);
    if (n < 0 && errno == EINTR)
    {
        return 0;
    }
    if (n < 0)
    {
        server_log_error("recv fallita fd=%d addr=%s: %s", c->fd, c->remote_addr, strerror(errno));
    }
    if (n <= 0)
    {
        disconnect_client(s, index);
        return -1;
    }
    if (c->inbuf_len + (size_t)n >= sizeof(c->inbuf))
    {
        server_log_error("linea troppo lunga ricevuta da nickname=%s addr=%s",
                         c->nickname[0] != '\0' ? c->nickname : "-",
                         c->remote_addr);
        sendf(c, "S2C_ERR LINE_TOO_LONG");
        disconnect_client(s, index);
        return -1;
    }
    memcpy(c->inbuf + c->inbuf_len, tmp, (size_t)n);
    c->inbuf_len += (size_t)n;
    c->inbuf[c->inbuf_len] = '\0';
    server_log_info("ricevuti %zd byte da fd=%d addr=%s", n, c->fd, c->remote_addr);

    while (1)
    {
        char *nl = memchr(c->inbuf, '\n', c->inbuf_len);
        size_t line_len;
        char line[PROTO_MAX_LINE];
        if (nl == NULL)
        {
            break;
        }
        line_len = (size_t)(nl - c->inbuf);
        if (line_len >= sizeof(line))
        {
            disconnect_client(s, index);
            return -1;
        }
        memcpy(line, c->inbuf, line_len);
        line[line_len] = '\0';
        memmove(c->inbuf, nl + 1, c->inbuf_len - line_len - 1);
        c->inbuf_len -= line_len + 1;
        c->inbuf[c->inbuf_len] = '\0';
        handle_line(s, index, line);
        if (c->fd < 0)
        {
            return -1;
        }
    }
    return 0;
}

// Invia a tutti i client il risultato finale della partita.
static void broadcast_game_over(server_t *s)
{
    size_t i;
    char winner[NICK_MAX + 1];
    char scores[PROTO_MAX_LINE];
    int score;

    game_winner(&s->game, winner, sizeof(winner), &score);
    if (game_build_scores(&s->game, scores, sizeof(scores)) != 0)
    {
        snprintf(scores, sizeof(scores), "-");
    }
    server_log_info("game over winner=%s score=%d scoreboard=%s", winner, score, scores);
    for (i = 0; i < s->client_count; ++i)
    {
        if (s->clients[i].fd >= 0)
        {
            sendf(&s->clients[i], "S2C_GAME_OVER %s %d %s", winner, score, scores);
        }
    }
}

// Calcola il timeout da passare a select per il prossimo evento temporizzato.
static long seconds_until_next_event(server_t *s)
{
    time_t now = time(NULL);
    time_t end = s->start_time + s->duration_sec;
    time_t next = s->next_update < end ? s->next_update : end;
    if (next <= now)
    {
        return 0;
    }
    return (long)(next - now);
}

// Avvia il server: socket di ascolto, ciclo select, timer e cleanup finale.
int server_run(const char *port, int duration_sec, int period_sec)
{
    server_t s;
    int listen_fd = net_create_server_socket(port);

    if (listen_fd < 0)
    {
        server_log_error("avvio server fallito sulla porta %s: %s", port, net_last_error());
        return -1;
    }
    if (listen_fd >= FD_SETSIZE)
    {
        server_log_error("listen fd=%d oltre FD_SETSIZE", listen_fd);
        close(listen_fd);
        return -1;
    }
    signal(SIGPIPE, SIG_IGN);
    server_init(&s, listen_fd, duration_sec, period_sec);
    server_log_info("server avviato porta=%s durata=%d periodo=%d", port, duration_sec, period_sec);

    while (s.running)
    {
        fd_set rfds;
        struct timeval tv;
        int maxfd = s.listen_fd;
        int rc;
        size_t i;
        time_t now;

        FD_ZERO(&rfds);
        FD_SET(s.listen_fd, &rfds);
        for (i = 0; i < s.client_count; ++i)
        {
            if (s.clients[i].fd >= 0)
            {
                FD_SET(s.clients[i].fd, &rfds);
                if (s.clients[i].fd > maxfd)
                {
                    maxfd = s.clients[i].fd;
                }
            }
        }

        tv.tv_sec = seconds_until_next_event(&s);
        tv.tv_usec = 0;
        server_log_info("attesa eventi timeout=%ld client_slots=%zu", (long)tv.tv_sec, s.client_count);
        // select multiplexa socket di ascolto, socket client e timer periodici.
        rc = select(maxfd + 1, &rfds, NULL, NULL, &tv);
        if (rc < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            server_log_error("select fallita: %s", strerror(errno));
            break;
        }
        if (rc > 0 && FD_ISSET(s.listen_fd, &rfds))
        {
            accept_client(&s);
        }
        for (i = 0; i < s.client_count; ++i)
        {
            if (s.clients[i].fd >= 0 && FD_ISSET(s.clients[i].fd, &rfds))
            {
                read_client(&s, i);
            }
        }

        now = time(NULL);
        if (now >= s.next_update)
        {
            server_log_info("broadcast globale ai client autenticati");
            broadcast_global(&s);
            s.next_update = now + s.period_sec;
        }
        if (now >= s.start_time + s.duration_sec)
        {
            server_log_info("tempo partita scaduto, invio game over");
            broadcast_game_over(&s);
            s.running = 0;
        }
    }

    for (size_t i = 0; i < s.client_count; ++i)
    {
        disconnect_client(&s, i);
    }
    close(s.listen_fd);
    server_free(&s);
    server_log_info("server arrestato");
    return 0;
}
