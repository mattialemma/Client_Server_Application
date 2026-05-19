#include "client/client.h"

#include "client/ui.h"
#include "common/net.h"
#include "common/protocol.h"
#include "common/utils.h"

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

typedef struct {
    // In questa struttura raccolgo tutto lo stato runtime del client, cosi
    // le funzioni non devono inseguire troppe variabili sparse.
    ui_state_t ui;
    char command_buffer[UI_INPUT_MAX];
    size_t command_length;
    int interactive_mode;
} client_runtime_t;

static struct termios g_saved_termios;
static int g_terminal_mode_saved = 0;

/* Gestione terminale */

static void restore_terminal_mode(void) {
    // Ripristina il terminale se il client lo aveva messo in modalita non canonica.
    if (g_terminal_mode_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &g_saved_termios);
        g_terminal_mode_saved = 0;
    }
}

static void activate_terminal_mode(void) {
    struct termios raw_mode;

    // In modalita interattiva leggiamo un byte alla volta, cosi w/a/s/d sono immediati.
    if (!isatty(STDIN_FILENO)) {
        return;
    }
    if (tcgetattr(STDIN_FILENO, &g_saved_termios) != 0) {
        return;
    }
    raw_mode = g_saved_termios;
    raw_mode.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw_mode.c_cc[VMIN] = 0;
    raw_mode.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw_mode) == 0) {
        g_terminal_mode_saved = 1;
    }
}

/* Invio comandi */

static int send_protocol_command(int socket_fd, const char *fmt, ...) {
    // Il protocollo applicativo e testuale e lavora "a righe": qui formatto il
    // comando una sola volta e mi assicuro che esca sempre gia pronto per la rete.
    char payload[PROTO_MAX_LINE];
    char framed_line[PROTO_MAX_LINE];
    va_list args;
    int written;

    va_start(args, fmt);
    written = vsnprintf(payload, sizeof(payload), fmt, args);
    va_end(args);
    if (written < 0 || (size_t)written >= sizeof(payload)) {
        return -1;
    }
    if (proto_make_line(framed_line, sizeof(framed_line), "%s", payload) < 0) {
        return -1;
    }
    return net_send_line(socket_fd, framed_line);
}

/* Parsing input */

static void strip_line_endings(char *text) {
    size_t length = strlen(text);

    while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r')) {
        text[--length] = '\0';
    }
}

static int parse_map_dimensions(char **tokens, long *width, long *height) {
    int width_ok;
    int height_ok;

    *width = utils_parse_long(tokens[1], 1, 200, &width_ok);
    *height = utils_parse_long(tokens[2], 1, 200, &height_ok);
    return width_ok && height_ok;
}

static int log_and_send_command(client_runtime_t *runtime, int socket_fd,
                                const char *event_message, const char *protocol_message) {
    // Questo helper mi evita di ripetere sempre la stessa coppia di operazioni:
    // aggiorno il registro eventi della UI e subito dopo invio il comando vero.
    ui_add_event(&runtime->ui, "%s", event_message);
    return send_protocol_command(socket_fd, "%s", protocol_message);
}

static int run_user_command(int socket_fd, client_runtime_t *runtime, char *raw_command) {
    // Qui faccio da "ponte" tra i comandi digitati dall'utente e i messaggi
    // C2S del protocollo. L'idea e semplice: riconosco il comando locale e lo
    // traduco nel testo che il server si aspetta.
    char command_copy[PROTO_MAX_LINE];
    char *tokens[PROTO_MAX_TOKENS];
    int token_count;
    direction_t direction;

    strip_line_endings(raw_command);
    snprintf(command_copy, sizeof(command_copy), "%s", raw_command);
    token_count = proto_split(command_copy, tokens, PROTO_MAX_TOKENS);
    if (token_count == 0) {
        return 0;
    }

    if (strcmp(tokens[0], "register") == 0 && token_count == 3) {
        char event_message[PROTO_MAX_LINE];
        char protocol_message[PROTO_MAX_LINE];

        snprintf(event_message, sizeof(event_message), "Registro il profilo %s", tokens[1]);
        snprintf(protocol_message, sizeof(protocol_message), "C2S_REGISTER %s %s", tokens[1], tokens[2]);
        return log_and_send_command(runtime, socket_fd, event_message, protocol_message);
    }
    if (strcmp(tokens[0], "login") == 0 && token_count == 3) {
        char event_message[PROTO_MAX_LINE];
        char protocol_message[PROTO_MAX_LINE];

        snprintf(event_message, sizeof(event_message), "Richiedo accesso per %s", tokens[1]);
        snprintf(protocol_message, sizeof(protocol_message), "C2S_LOGIN %s %s", tokens[1], tokens[2]);
        return log_and_send_command(runtime, socket_fd, event_message, protocol_message);
    }
    if (strcmp(tokens[0], "move") == 0 && token_count == 2 &&
        proto_parse_direction(tokens[1], &direction) == 0) {
        return send_protocol_command(socket_fd, "C2S_MOVE %s", proto_direction_name(direction));
    }
    if (token_count == 1 && proto_parse_direction(tokens[0], &direction) == 0) {
        return send_protocol_command(socket_fd, "C2S_MOVE %s", proto_direction_name(direction));
    }
    if ((strcmp(tokens[0], "users") == 0 || strcmp(tokens[0], "list") == 0 || strcmp(tokens[0], "l") == 0) &&
        token_count == 1) {
        return log_and_send_command(runtime, socket_fd,
                                    "Richiedo esploratori attivi",
                                    "C2S_LIST_USERS");
    }
    if (strcmp(tokens[0], "local") == 0 && token_count == 1) {
        return log_and_send_command(runtime, socket_fd,
                                    "Richiedo vista locale",
                                    "C2S_LOCAL_MAP");
    }
    if (strcmp(tokens[0], "global") == 0 && token_count == 1) {
        return log_and_send_command(runtime, socket_fd,
                                    "Richiedo mappa del labirinto",
                                    "C2S_GLOBAL_MAP");
    }
    if ((strcmp(tokens[0], "help") == 0 || strcmp(tokens[0], "h") == 0) && token_count == 1) {
        ui_add_event(&runtime->ui, "Guida visibile nel pannello comandi");
        return 0;
    }
    if ((strcmp(tokens[0], "quit") == 0 || strcmp(tokens[0], "q") == 0) && token_count == 1) {
        ui_add_event(&runtime->ui, "Richiesta di uscita");
        if (send_protocol_command(socket_fd, "C2S_QUIT") != 0) {
            return -1;
        }
        return 1;
    }

    ui_add_event(&runtime->ui, "Comando ignoto: %s", tokens[0]);
    return 0;
}

static int handle_typed_byte(int socket_fd, client_runtime_t *runtime, unsigned char byte_read) {
    char single_command[UI_INPUT_MAX];
    direction_t direction;

    // Gestisco l'input un byte alla volta per permettere sia i tasti rapidi
    // sia la modalita "comando completo" confermata con Invio.
    if (byte_read == 4) {
        send_protocol_command(socket_fd, "C2S_QUIT");
        return 1;
    }
    if (runtime->command_length == 0 &&
        (byte_read == 'w' || byte_read == 'a' || byte_read == 's' || byte_read == 'd')) {
        single_command[0] = (char)byte_read;
        single_command[1] = '\0';
        if (proto_parse_direction(single_command, &direction) == 0) {
            return send_protocol_command(socket_fd, "C2S_MOVE %s", proto_direction_name(direction));
        }
    }
    if (byte_read == '\r' || byte_read == '\n') {
        snprintf(single_command, sizeof(single_command), "%s", runtime->command_buffer);
        runtime->command_buffer[0] = '\0';
        runtime->command_length = 0;
        return run_user_command(socket_fd, runtime, single_command);
    }
    if (byte_read == 127 || byte_read == 8) {
        if (runtime->command_length > 0) {
            runtime->command_buffer[--runtime->command_length] = '\0';
            if (runtime->interactive_mode) {
                fputs("\b \b", stdout);
                fflush(stdout);
            }
        }
        return 0;
    }
    if (isprint(byte_read) && runtime->command_length + 1 < sizeof(runtime->command_buffer)) {
        runtime->command_buffer[runtime->command_length++] = (char)byte_read;
        runtime->command_buffer[runtime->command_length] = '\0';
        if (runtime->interactive_mode) {
            putchar(byte_read);
            fflush(stdout);
        }
    }
    return 0;
}

static int consume_stdin(int socket_fd, client_runtime_t *runtime, int *completed_command) {
    // Questo punto unifica due casi diversi: tastiera interattiva e input
    // arrivato da pipe/file. In entrambi i casi scorro i byte e li passo
    // allo stesso gestore, cosi il comportamento resta coerente.
    unsigned char input_chunk[128];
    ssize_t bytes_read;
    ssize_t index;
    int result = 0;

    *completed_command = 0;
    bytes_read = read(STDIN_FILENO, input_chunk, sizeof(input_chunk));
    if (bytes_read == 0) {
        send_protocol_command(socket_fd, "C2S_QUIT");
        return 1;
    }
    if (bytes_read < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            return 0;
        }
        return -1;
    }
    for (index = 0; index < bytes_read; ++index) {
        result = handle_typed_byte(socket_fd, runtime, input_chunk[index]);
        if (input_chunk[index] == '\r' || input_chunk[index] == '\n') {
            *completed_command = 1;
        }
        if (result != 0) {
            return result;
        }
    }
    return 0;
}

/* Parsing messaggi server */

static void apply_server_message(client_runtime_t *runtime, const char *server_line) {
    // Qui faccio l'operazione opposta a run_user_command(): prendo una riga
    // S2C dal server e aggiorno lo stato locale della UI in base al contenuto.
    char line_copy[PROTO_MAX_LINE];
    char *tokens[PROTO_MAX_TOKENS];
    int token_count;
    long width;
    long height;
    long x;
    long y;

    snprintf(line_copy, sizeof(line_copy), "%s", server_line);
    token_count = proto_split(line_copy, tokens, PROTO_MAX_TOKENS);
    if (token_count == 0) {
        return;
    }

    if (strcmp(tokens[0], "S2C_LOCAL_MAP") == 0 && token_count == 4) {
        if (parse_map_dimensions(tokens, &width, &height)) {
            ui_set_local_map(&runtime->ui, (int)width, (int)height, tokens[3]);
            ui_add_event(&runtime->ui, "Vista locale sincronizzata");
        }
    } else if (strcmp(tokens[0], "S2C_GLOBAL_UPDATE") == 0 && token_count == 5) {
        if (parse_map_dimensions(tokens, &width, &height)) {
            ui_set_global_map(&runtime->ui, (int)width, (int)height, tokens[3], tokens[4]);
            ui_add_event(&runtime->ui, "Impulso globale ricevuto");
        }
    } else if (strcmp(tokens[0], "S2C_USERS") == 0 && token_count == 2) {
        ui_set_positions(&runtime->ui, tokens[1]);
        ui_add_event(&runtime->ui, "Esploratori attivi: %s", tokens[1]);
    } else if (strcmp(tokens[0], "S2C_GAME_OVER") == 0 && token_count == 4) {
        ui_set_game_over(&runtime->ui, tokens[1], tokens[2], tokens[3]);
        ui_add_event(&runtime->ui, "Sessione del labirinto conclusa");
    } else if (strcmp(tokens[0], "S2C_OK") == 0 && token_count >= 2) {
        if (strcmp(tokens[1], "CONNECTED") == 0) {
            ui_set_connected(&runtime->ui, 1);
            ui_add_event(&runtime->ui, "Canale con il server aperto");
        } else if (strcmp(tokens[1], "REGISTERED") == 0) {
            ui_add_event(&runtime->ui, "Profilo esploratore registrato");
        } else if (strcmp(tokens[1], "LOGGED_IN") == 0 && token_count == 6) {
            int x_ok;
            int y_ok;

            x = utils_parse_long(tokens[4], 0, 10000, &x_ok);
            y = utils_parse_long(tokens[5], 0, 10000, &y_ok);
            ui_set_user(&runtime->ui, tokens[2], tokens[3]);
            if (x_ok && y_ok) {
                ui_set_position(&runtime->ui, (int)x, (int)y);
            }
            ui_add_event(&runtime->ui, "Ingresso nel labirinto confermato");
        } else if (strcmp(tokens[1], "MOVED") == 0 && token_count == 4) {
            int x_ok;
            int y_ok;

            x = utils_parse_long(tokens[2], 0, 10000, &x_ok);
            y = utils_parse_long(tokens[3], 0, 10000, &y_ok);
            if (x_ok && y_ok) {
                ui_set_position(&runtime->ui, (int)x, (int)y);
                ui_add_event(&runtime->ui, "Passo eseguito verso (%ld,%ld)", x, y);
            }
        } else if (strcmp(tokens[1], "BYE") == 0) {
            ui_add_event(&runtime->ui, "Uscita dal labirinto confermata");
        } else {
            ui_add_event(&runtime->ui, "Segnale OK dal server");
        }
    } else if (strcmp(tokens[0], "S2C_ERR") == 0 && token_count >= 2) {
        ui_add_event(&runtime->ui, "Avviso: %s", tokens[1]);
    } else {
        ui_add_event(&runtime->ui, "Segnale server non riconosciuto");
    }
}

static int consume_server_socket(int socket_fd, client_runtime_t *runtime,
                                 char *receive_buffer, size_t *receive_length) {
    // TCP non conserva i confini dei messaggi, quindi qui tengo un buffer di
    // accumulo e considero "completa" una risposta solo quando trovo '\n'.
    char incoming_chunk[512];
    ssize_t bytes_read = recv(socket_fd, incoming_chunk, sizeof(incoming_chunk), 0);

    if (bytes_read < 0 && errno == EINTR) {
        return 0;
    }
    if (bytes_read <= 0) {
        ui_set_connected(&runtime->ui, 0);
        ui_add_event(&runtime->ui, "Canale chiuso dal server");
        return -1;
    }
    if (*receive_length + (size_t)bytes_read >= PROTO_MAX_LINE) {
        ui_add_event(&runtime->ui, "Segnale server troppo lungo");
        return -1;
    }
    memcpy(receive_buffer + *receive_length, incoming_chunk, (size_t)bytes_read);
    *receive_length += (size_t)bytes_read;
    receive_buffer[*receive_length] = '\0';

    while (1) {
        char *line_break = memchr(receive_buffer, '\n', *receive_length);
        char complete_line[PROTO_MAX_LINE];
        size_t line_length;

        if (line_break == NULL) {
            break;
        }
        line_length = (size_t)(line_break - receive_buffer);
        memcpy(complete_line, receive_buffer, line_length);
        complete_line[line_length] = '\0';
        memmove(receive_buffer, line_break + 1, *receive_length - line_length - 1);
        *receive_length -= line_length + 1;
        receive_buffer[*receive_length] = '\0';
        apply_server_message(runtime, complete_line);
    }
    return 0;
}

/* Loop principale */

int client_run(const char *host, const char *port) {
    int server_fd = net_connect_tcp(host, port);
    char socket_buffer[PROTO_MAX_LINE];
    size_t socket_buffer_length = 0;
    int keep_running = 1;
    int exit_code = 0;
    client_runtime_t runtime;

    if (server_fd < 0) {
        return -1;
    }

    // Ignoro SIGPIPE per evitare che il processo termini brutalmente se provo
    // a scrivere su una connessione che il server ha gia chiuso.
    signal(SIGPIPE, SIG_IGN);
    memset(&runtime, 0, sizeof(runtime));
    runtime.interactive_mode = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
    ui_init(&runtime.ui, host, port);
    ui_set_connected(&runtime.ui, 1);
    ui_add_event(&runtime.ui, "Connesso a %s:%s", host, port);

    activate_terminal_mode();
    ui_render(&runtime.ui, runtime.command_buffer);

    // Questo e il cuore del client: con select aspetto insieme sia la rete sia
    // la tastiera, cosi posso ricevere aggiornamenti mentre l'utente digita.
    while (keep_running) {
        fd_set readable_fds;
        int max_fd = server_fd > STDIN_FILENO ? server_fd : STDIN_FILENO;
        int select_result;
        int should_render = 0;

        FD_ZERO(&readable_fds);
        FD_SET(server_fd, &readable_fds);
        FD_SET(STDIN_FILENO, &readable_fds);
        // Qui tengo bloccato il client finche non succede qualcosa di utile:
        // o arriva un messaggio dal server, o l'utente preme un tasto.
        select_result = select(max_fd + 1, &readable_fds, NULL, NULL, NULL);
        if (select_result < 0) {
            if (errno == EINTR) {
                continue;
            }
            exit_code = -1;
            break;
        }
        if (FD_ISSET(server_fd, &readable_fds)) {
            if (consume_server_socket(server_fd, &runtime, socket_buffer, &socket_buffer_length) != 0) {
                keep_running = 0;
            }
            should_render = 1;
        }
        if (keep_running && FD_ISSET(STDIN_FILENO, &readable_fds)) {
            int command_completed = 0;

            select_result = consume_stdin(server_fd, &runtime, &command_completed);
            if (select_result < 0) {
                exit_code = -1;
                keep_running = 0;
            } else if (select_result > 0) {
                keep_running = 0;
            }
            should_render = command_completed;
        }

        if (should_render) {
            ui_render(&runtime.ui, runtime.command_buffer);
        }
    }

    ui_render(&runtime.ui, runtime.command_buffer);
    restore_terminal_mode();
    ui_finish();
    close(server_fd);
    return exit_code;
}
