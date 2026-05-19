#include "server/server.h"

#include "common/net.h"
#include "common/protocol.h"
#include "common/utils.h"

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define INITIAL_CLIENTS_CAPACITY 16

/* Stato base e memoria */

// Quando libero una sessione rimetto tutto in uno stato neutro, cosi quello
// slot puo essere riusato senza trascinarsi dietro dati del client precedente.
static void clear_client_session(client_session_t *session)
{
    session->fd = -1;
    session->authenticated = 0;
    session->player_id = -1;
    session->inbuf_len = 0;
}

// Qui preparo il contenitore generale del server: socket di ascolto, timer di
// partita, database utenti e stato del gioco vero e proprio.
static void initialize_server_state(server_t *server, int listen_fd, int duration_sec, int period_sec)
{
    memset(server, 0, sizeof(*server));
    server->listen_fd = listen_fd;
    server->duration_sec = duration_sec;
    server->period_sec = period_sec;
    server->running = 1;
    server->start_time = time(NULL);
    server->next_update = server->start_time + period_sec;
    game_init(&server->game);
}

// Libera array dinamici e sottostrutture possedute dal server.
static void release_server_state(server_t *server)
{
    free(server->clients);
    users_free(&server->users);
    game_free(&server->game);
    server->clients = NULL;
    server->client_count = 0;
    server->client_capacity = 0;
}

// Garantisce spazio per almeno needed sessioni client.
static int ensure_client_capacity(server_t *server, size_t needed)
{
    // Le sessioni crescono dinamicamente; il limite pratico resta quello di select/OS.
    client_session_t *resized_sessions;
    size_t index;
    size_t old_capacity = server->client_capacity;
    size_t new_capacity = old_capacity == 0 ? INITIAL_CLIENTS_CAPACITY : old_capacity;

    while (new_capacity < needed)
    {
        new_capacity *= 2;
    }
    if (new_capacity == old_capacity)
    {
        return 0;
    }
    resized_sessions = realloc(server->clients, new_capacity * sizeof(*resized_sessions));
    if (resized_sessions == NULL)
    {
        return -1;
    }
    server->clients = resized_sessions;
    for (index = old_capacity; index < new_capacity; ++index)
    {
        clear_client_session(&server->clients[index]);
    }
    server->client_capacity = new_capacity;
    return 0;
}

static void disconnect_session(server_t *server, int session_index);

/* Invio risposte */

// Tutte le risposte del server passano da qui, cosi ho un solo punto che si
// occupa di formattare la riga S2C e spedirla sul socket del client.
static int send_server_message(client_session_t *session, const char *fmt, ...)
{
    char payload[PROTO_MAX_LINE];
    char protocol_line[PROTO_MAX_LINE];
    va_list args;
    int written;

    va_start(args, fmt);
    written = vsnprintf(payload, sizeof(payload), fmt, args);
    va_end(args);
    if (written < 0 || (size_t)written >= sizeof(payload))
    {
        return -1;
    }
    if (proto_make_line(protocol_line, sizeof(protocol_line), "%s", payload) < 0)
    {
        return -1;
    }
    if (net_send_line(session->fd, protocol_line) != 0)
    {
        return -1;
    }
    return 0;
}

// La vista locale dipende dal giocatore autenticato, quindi la invio solo
// se la sessione e valida e collegata a uno slot di gioco attivo.
static int send_local_view(server_t *server, client_session_t *session)
{
    char encoded_map[PROTO_MAX_LINE];

    if (session->authenticated && session->player_id >= 0)
    {
        if (game_build_local_map(&server->game, session->player_id, encoded_map, sizeof(encoded_map)) != 0)
        {
            return send_server_message(session, "S2C_ERR ENCODING_FAILED");
        }
        return send_server_message(session, "S2C_LOCAL_MAP %d %d %s", LOCAL_VIEW_W, LOCAL_VIEW_H, encoded_map);
    }
    return 0;
}

// La vista globale e pubblica: mostra i territori e le posizioni, ma non i muri.
static int send_global_view(server_t *server, client_session_t *session)
{
    char encoded_map[PROTO_MAX_LINE];
    char encoded_positions[PROTO_MAX_LINE];

    if (session->fd >= 0 && session->authenticated)
    {
        if (game_build_global_map(&server->game, encoded_map, sizeof(encoded_map)) != 0 ||
            game_build_positions(&server->game, encoded_positions, sizeof(encoded_positions)) != 0)
        {
            return send_server_message(session, "S2C_ERR ENCODING_FAILED");
        }
        return send_server_message(session, "S2C_GLOBAL_UPDATE %d %d %s %s",
                                   MAP_W, MAP_H, encoded_map, encoded_positions);
    }
    return 0;
}

// Questo broadcast e l'aggiornamento periodico della partita. Se un client non
// riesce piu a ricevere, preferisco scollegarlo e mantenere coerente il server.
static void broadcast_global_view(server_t *server)
{
    size_t index;

    for (index = 0; index < server->client_count; ++index)
    {
        if (send_global_view(server, &server->clients[index]) != 0)
        {
            disconnect_session(server, (int)index);
        }
    }
}

// Alla fine della partita invio a tutti lo stesso riepilogo, cosi ogni client
// puo chiudere la sessione mostrando vincitore e punteggi finali.
static void broadcast_game_summary(server_t *server)
{
    size_t index;
    char winner_name[NICK_MAX + 1];
    char encoded_scores[PROTO_MAX_LINE];
    int winner_score;

    game_winner(&server->game, winner_name, sizeof(winner_name), &winner_score);
    if (game_build_scores(&server->game, encoded_scores, sizeof(encoded_scores)) != 0)
    {
        snprintf(encoded_scores, sizeof(encoded_scores), "-");
    }
    for (index = 0; index < server->client_count; ++index)
    {
        if (server->clients[index].fd >= 0)
        {
            send_server_message(&server->clients[index], "S2C_GAME_OVER %s %d %s",
                                winner_name, winner_score, encoded_scores);
        }
    }
}

/* Connessioni */

// Disconnettere non significa cancellare tutto: chiudo il socket, segno il
// giocatore offline, ma lascio intatti i territori che aveva gia conquistato.
static void disconnect_session(server_t *server, int session_index)
{
    client_session_t *session = &server->clients[session_index];

    if (session->fd >= 0)
    {
        close(session->fd);
    }
    if (session->authenticated && session->player_id >= 0)
    {
        // Il giocatore diventa offline, ma i territori restano assegnati al suo slot.
        game_remove_player(&server->game, session->player_id);
    }
    clear_client_session(session);
}

// Quando arriva un nuovo client cerco prima uno slot libero gia esistente; se
// non lo trovo espando l'array delle sessioni e ne creo uno nuovo.
static void accept_new_client(server_t *server)
{
    int client_fd;
    size_t index;
    size_t free_slot;

    client_fd = accept(server->listen_fd, NULL, NULL);
    if (client_fd < 0)
    {
        return;
    }
    // select(2) usa fd_set: descrittori oltre FD_SETSIZE non sono gestibili.
    if (client_fd >= FD_SETSIZE)
    {
        net_send_line(client_fd, "S2C_ERR SERVER_FULL\n");
        close(client_fd);
        return;
    }
    for (index = 0; index < server->client_count; ++index)
    {
        if (server->clients[index].fd < 0)
        {
            clear_client_session(&server->clients[index]);
            server->clients[index].fd = client_fd;
            send_server_message(&server->clients[index], "S2C_OK CONNECTED");
            return;
        }
    }
    free_slot = server->client_count;
    if (ensure_client_capacity(server, server->client_count + 1) != 0)
    {
        net_send_line(client_fd, "S2C_ERR SERVER_FULL\n");
        close(client_fd);
        return;
    }
    server->client_count++;
    clear_client_session(&server->clients[free_slot]);
    server->clients[free_slot].fd = client_fd;
    send_server_message(&server->clients[free_slot], "S2C_OK CONNECTED");
}

/* Validazioni e comandi */

// Alcuni comandi hanno senso solo dopo il login: con questo controllo evito di
// ripetere la stessa guardia in tanti punti diversi.
static int require_authenticated_session(client_session_t *session)
{
    if (!session->authenticated)
    {
        send_server_message(session, "S2C_ERR NOT_AUTHENTICATED");
        return 0;
    }
    return 1;
}

// Register tocca solo il database utenti: qui non creo ancora nessun giocatore
// nella partita, mi limito a validare e salvare le credenziali.
static void handle_register_command(server_t *server, client_session_t *session,
                                    char **tokens, int token_count)
{
    int result;

    if (token_count != 3)
    {
        send_server_message(session, "S2C_ERR BAD_SYNTAX");
        return;
    }
    result = users_register(&server->users, tokens[1], tokens[2]);
    if (result == 0)
    {
        send_server_message(session, "S2C_OK REGISTERED");
    }
    else if (result == -1)
    {
        send_server_message(session, "S2C_ERR USER_EXISTS");
    }
    else if (result == -2)
    {
        send_server_message(session, "S2C_ERR INVALID_CREDENTIALS");
    }
    else
    {
        send_server_message(session, "S2C_ERR USER_DB_FULL");
    }
}

// Login e il punto in cui collego autenticazione e gioco: se le credenziali
// sono corrette, allora assegno anche uno slot/posizione dentro la mappa.
static void handle_login_command(server_t *server, client_session_t *session,
                                 char **tokens, int token_count)
{
    int player_id;

    if (token_count != 3)
    {
        send_server_message(session, "S2C_ERR BAD_SYNTAX");
        return;
    }
    if (session->authenticated)
    {
        send_server_message(session, "S2C_ERR ALREADY_AUTHENTICATED");
        return;
    }
    if (users_authenticate(&server->users, tokens[1], tokens[2]) != 0)
    {
        send_server_message(session, "S2C_ERR AUTH_FAILED");
        return;
    }
    if (game_find_player(&server->game, tokens[1]) >= 0)
    {
        send_server_message(session, "S2C_ERR USER_ALREADY_ONLINE");
        return;
    }
    player_id = game_add_player(&server->game, tokens[1]);
    if (player_id < 0)
    {
        send_server_message(session, "S2C_ERR GAME_FULL");
        return;
    }
    session->authenticated = 1;
    session->player_id = player_id;
    send_server_message(session, "S2C_OK LOGGED_IN %s %s %d %d",
                        tokens[1],
                        server->game.players[player_id].symbol,
                        server->game.players[player_id].x,
                        server->game.players[player_id].y);
    send_local_view(server, session);
    broadcast_global_view(server);
}

static void send_move_result(client_session_t *session, const game_t *game, int move_result)
{
    // game_move() mi restituisce codici interni; qui li traduco nei messaggi
    // leggibili dal protocollo senza sporcare troppo handle_move_command().
    if (move_result == 0)
    {
        send_server_message(session, "S2C_OK MOVED %d %d",
                            game->players[session->player_id].x,
                            game->players[session->player_id].y);
    }
    else if (move_result == -2)
    {
        send_server_message(session, "S2C_ERR OUT_OF_BOUNDS");
    }
    else if (move_result == -3)
    {
        send_server_message(session, "S2C_ERR WALL");
    }
    else if (move_result == -4)
    {
        send_server_message(session, "S2C_ERR OCCUPIED");
    }
    else
    {
        send_server_message(session, "S2C_ERR MOVE_FAILED");
    }
}

// Move ha un flusso lineare: controllo login, valido la direzione, provo il
// movimento e poi aggiorno sempre la vista locale del giocatore.
static void handle_move_command(server_t *server, client_session_t *session,
                                char **tokens, int token_count)
{
    direction_t direction;
    int result;

    if (!require_authenticated_session(session))
    {
        return;
    }
    if (token_count != 2 || proto_parse_direction(tokens[1], &direction) != 0)
    {
        send_server_message(session, "S2C_ERR BAD_DIRECTION");
        return;
    }
    result = game_move(&server->game, session->player_id, direction);
    send_move_result(session, &server->game, result);
    send_local_view(server, session);
}

// Questo comando non interroga un database separato: serializzo semplicemente
// i giocatori online partendo dallo stato della partita.
static void handle_users_command(server_t *server, client_session_t *session)
{
    char encoded_positions[PROTO_MAX_LINE];

    if (!require_authenticated_session(session))
    {
        return;
    }
    if (game_build_positions(&server->game, encoded_positions, sizeof(encoded_positions)) != 0)
    {
        send_server_message(session, "S2C_ERR ENCODING_FAILED");
        return;
    }
    send_server_message(session, "S2C_USERS %s", encoded_positions);
}

// Questo e il punto in cui "smonto" una riga ricevuta dal client e la porto
// al gestore corretto in base al comando C2S.
static void dispatch_client_command(server_t *server, int session_index, char *line)
{
    client_session_t *session = &server->clients[session_index];
    char *tokens[PROTO_MAX_TOKENS];
    int token_count = proto_split(line, tokens, PROTO_MAX_TOKENS);

    if (token_count == 0)
    {
        return;
    }
    if (strcmp(tokens[0], "C2S_REGISTER") == 0)
    {
        handle_register_command(server, session, tokens, token_count);
    }
    else if (strcmp(tokens[0], "C2S_LOGIN") == 0)
    {
        handle_login_command(server, session, tokens, token_count);
    }
    else if (strcmp(tokens[0], "C2S_MOVE") == 0)
    {
        handle_move_command(server, session, tokens, token_count);
    }
    else if (strcmp(tokens[0], "C2S_LIST_USERS") == 0 && token_count == 1)
    {
        handle_users_command(server, session);
    }
    else if (strcmp(tokens[0], "C2S_LOCAL_MAP") == 0 && token_count == 1)
    {
        if (require_authenticated_session(session))
        {
            send_local_view(server, session);
        }
    }
    else if (strcmp(tokens[0], "C2S_GLOBAL_MAP") == 0 && token_count == 1)
    {
        if (require_authenticated_session(session))
        {
            if (send_global_view(server, session) != 0)
            {
                disconnect_session(server, session_index);
            }
        }
    }
    else if (strcmp(tokens[0], "C2S_QUIT") == 0 && token_count == 1)
    {
        send_server_message(session, "S2C_OK BYE");
        disconnect_session(server, session_index);
    }
    else if (strncmp(tokens[0], "C2S_", 4) == 0)
    {
        send_server_message(session, "S2C_ERR BAD_SYNTAX");
    }
    else
    {
        send_server_message(session, "S2C_ERR UNKNOWN_COMMAND");
    }
}

/* Lettura socket e timer */

// Anche lato server devo ricordarmi che TCP e uno stream: per questo tengo
// un buffer per sessione e processo una riga completa alla volta.
static int process_client_socket(server_t *server, int session_index)
{
    client_session_t *session = &server->clients[session_index];
    char incoming_chunk[512];
    ssize_t bytes_read;

    bytes_read = recv(session->fd, incoming_chunk, sizeof(incoming_chunk), 0);
    if (bytes_read < 0 && errno == EINTR)
    {
        return 0;
    }
    if (bytes_read <= 0)
    {
        disconnect_session(server, session_index);
        return -1;
    }
    if (session->inbuf_len + (size_t)bytes_read >= sizeof(session->inbuf))
    {
        send_server_message(session, "S2C_ERR LINE_TOO_LONG");
        disconnect_session(server, session_index);
        return -1;
    }
    memcpy(session->inbuf + session->inbuf_len, incoming_chunk, (size_t)bytes_read);
    session->inbuf_len += (size_t)bytes_read;
    session->inbuf[session->inbuf_len] = '\0';

    while (1)
    {
        char *line_break = memchr(session->inbuf, '\n', session->inbuf_len);
        size_t line_length;
        char complete_line[PROTO_MAX_LINE];

        if (line_break == NULL)
        {
            break;
        }
        line_length = (size_t)(line_break - session->inbuf);
        if (line_length >= sizeof(complete_line))
        {
            disconnect_session(server, session_index);
            return -1;
        }
        memcpy(complete_line, session->inbuf, line_length);
        complete_line[line_length] = '\0';
        memmove(session->inbuf, line_break + 1, session->inbuf_len - line_length - 1);
        session->inbuf_len -= line_length + 1;
        session->inbuf[session->inbuf_len] = '\0';
        dispatch_client_command(server, session_index, complete_line);
        if (session->fd < 0)
        {
            return -1;
        }
    }
    return 0;
}

// select non mi serve solo per i socket: qui lo uso anche per "svegliarmi"
// quando arriva il momento del prossimo broadcast o della fine partita.
static long seconds_until_next_server_event(server_t *server)
{
    time_t now = time(NULL);
    time_t end_time = server->start_time + server->duration_sec;
    time_t next_deadline = server->next_update < end_time ? server->next_update : end_time;

    if (next_deadline <= now)
    {
        return 0;
    }
    return (long)(next_deadline - now);
}

/* Loop principale */

// Questo e il ciclo principale del server: ascolto nuove connessioni, leggo i
// client gia presenti e gestisco gli eventi temporizzati della partita.
int server_run(const char *port, int duration_sec, int period_sec)
{
    server_t server;
    int listen_fd = net_create_server_socket(port);

    if (listen_fd < 0)
    {
        return -1;
    }
    if (listen_fd >= FD_SETSIZE)
    {
        close(listen_fd);
        return -1;
    }
    signal(SIGPIPE, SIG_IGN);
    initialize_server_state(&server, listen_fd, duration_sec, period_sec);

    while (server.running)
    {
        fd_set readable_fds;
        struct timeval timeout;
        int max_fd = server.listen_fd;
        int select_result;
        size_t session_index;
        time_t now;

        FD_ZERO(&readable_fds);
        FD_SET(server.listen_fd, &readable_fds);
        for (session_index = 0; session_index < server.client_count; ++session_index)
        {
            if (server.clients[session_index].fd >= 0)
            {
                FD_SET(server.clients[session_index].fd, &readable_fds);
                if (server.clients[session_index].fd > max_fd)
                {
                    max_fd = server.clients[session_index].fd;
                }
            }
        }

        timeout.tv_sec = seconds_until_next_server_event(&server);
        timeout.tv_usec = 0;
        // select multiplexa socket di ascolto, socket client e timer periodici.
        select_result = select(max_fd + 1, &readable_fds, NULL, NULL, &timeout);
        if (select_result < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            break;
        }
        if (select_result > 0 && FD_ISSET(server.listen_fd, &readable_fds))
        {
            accept_new_client(&server);
        }
        for (session_index = 0; session_index < server.client_count; ++session_index)
        {
            if (server.clients[session_index].fd >= 0 &&
                FD_ISSET(server.clients[session_index].fd, &readable_fds))
            {
                process_client_socket(&server, (int)session_index);
            }
        }

        now = time(NULL);
        if (now >= server.next_update)
        {
            broadcast_global_view(&server);
            server.next_update = now + server.period_sec;
        }
        if (now >= server.start_time + server.duration_sec)
        {
            broadcast_game_summary(&server);
            server.running = 0;
        }
    }

    for (size_t session_index = 0; session_index < server.client_count; ++session_index)
    {
        disconnect_session(&server, (int)session_index);
    }
    close(server.listen_fd);
    release_server_state(&server);
    return 0;
}
