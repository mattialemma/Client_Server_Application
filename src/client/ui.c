#include "client/ui.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define ANSI_RESET "\033[0m"
#define ANSI_DIM "\033[2m"
#define ANSI_PLAYER "\033[1;32m"
#define ANSI_WALL "\033[1;37m"

#define CELL_CURRENT_PLAYER "☻"
#define CELL_OWNED "▒"
#define CELL_WALL "█"
#define CELL_FREE "."

static void safe_copy(char *dst, size_t dst_size, const char *src) {
    if (dst_size == 0) {
        return;
    }
    if (src == NULL) {
        src = "";
    }
    snprintf(dst, dst_size, "%s", src);
}

static void print_rule(void) {
    puts("============================================================");
}

void ui_clear_screen(void) {
    fputs("\033[2J\033[3J\033[H", stdout);
    fflush(stdout);
}

static const char *owner_color(char c) {
    static const char *palette[] = {
        "\033[1;31m",
        "\033[1;34m",
        "\033[1;33m",
        "\033[1;35m",
        "\033[1;36m",
        "\033[1;32m"
    };
    unsigned char uc = (unsigned char)c;
    return palette[uc % (sizeof(palette) / sizeof(palette[0]))];
}

static char current_owner_symbol(const ui_state_t *ui) {
    size_t nick_len;
    const char *p;

    if (ui->nickname[0] == '\0' || strcmp(ui->nickname, "-") == 0) {
        return '@';
    }

    nick_len = strlen(ui->nickname);
    p = ui->positions;
    while (p != NULL && *p != '\0') {
        if (strncmp(p, ui->nickname, nick_len) == 0 && p[nick_len] == ':' && p[nick_len + 1] != '\0') {
            return p[nick_len + 1];
        }
        p = strchr(p, ',');
        if (p != NULL) {
            ++p;
        }
    }

    return '@';
}

static int is_owner_symbol(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static const char *cell_glyph(char c) {
    if (c == '@') {
        return CELL_CURRENT_PLAYER;
    }
    if (c == '#') {
        return CELL_WALL;
    }
    if (c == '.') {
        return CELL_FREE;
    }
    if (is_owner_symbol(c)) {
        return CELL_OWNED;
    }
    return NULL;
}

static void print_colored_cell(char c, int color_enabled, char current_symbol) {
    const char *glyph = cell_glyph(c);
    const char *color;

    if (glyph == NULL) {
        putchar(c);
        return;
    }

    if (!color_enabled) {
        printf("%s", glyph);
        return;
    }

    if (c == '@') {
        color = is_owner_symbol(current_symbol) ? owner_color(current_symbol) : ANSI_PLAYER;
        printf("%s%s%s", color, glyph, ANSI_RESET);
    } else if (c == '#') {
        printf("%s%s%s", ANSI_WALL, glyph, ANSI_RESET);
    } else if (c == '.') {
        printf("%s%s%s", ANSI_DIM, glyph, ANSI_RESET);
    } else if (is_owner_symbol(c)) {
        printf("%s%s%s", owner_color(c), glyph, ANSI_RESET);
    } else {
        putchar(c);
    }
}

static void print_legend(int color_enabled) {
    printf("Legenda: ");
    print_colored_cell('@', color_enabled, 'A');
    printf(" tu  ");
    print_colored_cell('#', color_enabled, 'A');
    printf(" muro  ");
    print_colored_cell('.', color_enabled, 'A');
    printf(" libero  ");
    print_colored_cell('A', color_enabled, 'A');
    printf("/");
    print_colored_cell('B', color_enabled, 'A');
    printf("/... territori\n");
}

static void print_map_block(const char *title, int w, int h, const char *encoded, int color_enabled, char current_symbol) {
    int rows = 0;
    int col = 0;
    const char *p = encoded;

    printf("%s (%dx%d)\n", title, w, h);
    if (encoded == NULL || encoded[0] == '\0') {
        puts("  non ancora disponibile");
        return;
    }

    while (*p != '\0' && rows < h) {
        if (*p == '/') {
            if (col != 0) {
                putchar('\n');
                rows++;
                col = 0;
            }
        } else {
            print_colored_cell(*p, color_enabled, current_symbol);
            col++;
            if (col == w) {
                putchar('\n');
                rows++;
                col = 0;
            }
        }
        ++p;
    }
    if (col != 0) {
        putchar('\n');
    }
}

static void print_events(const ui_state_t *ui) {
    int start;
    int count;
    int i;

    puts("Ultimi eventi:");
    if (ui->event_count == 0) {
        puts("  - In attesa di eventi dal server");
        return;
    }

    count = ui->event_count < UI_EVENTS_MAX ? ui->event_count : UI_EVENTS_MAX;
    start = ui->event_count < UI_EVENTS_MAX ? 0 : ui->event_next;
    for (i = 0; i < count; ++i) {
        int idx = (start + i) % UI_EVENTS_MAX;
        printf("  - %s\n", ui->events[idx]);
    }
}

void ui_init(ui_state_t *ui, const char *host, const char *port) {
    memset(ui, 0, sizeof(*ui));
    snprintf(ui->server, sizeof(ui->server), "%s:%s", host, port);
    safe_copy(ui->nickname, sizeof(ui->nickname), "-");
    safe_copy(ui->positions, sizeof(ui->positions), "-");
    safe_copy(ui->status, sizeof(ui->status), "connessione in corso");
    ui->show_help = 1;
}

void ui_set_connected(ui_state_t *ui, int connected) {
    ui->connected = connected;
    safe_copy(ui->status, sizeof(ui->status), connected ? "connesso" : "disconnesso");
}

void ui_set_user(ui_state_t *ui, const char *nickname) {
    ui->authenticated = 1;
    safe_copy(ui->nickname, sizeof(ui->nickname), nickname);
    safe_copy(ui->status, sizeof(ui->status), "autenticato");
}

void ui_set_position(ui_state_t *ui, int x, int y) {
    ui->position_known = 1;
    ui->x = x;
    ui->y = y;
}

void ui_set_local_map(ui_state_t *ui, int w, int h, const char *encoded) {
    ui->local_valid = 1;
    ui->local_w = w;
    ui->local_h = h;
    safe_copy(ui->local_map, sizeof(ui->local_map), encoded);
}

void ui_set_global_map(ui_state_t *ui, int w, int h, const char *encoded, const char *positions) {
    ui->global_valid = 1;
    ui->global_w = w;
    ui->global_h = h;
    safe_copy(ui->global_map, sizeof(ui->global_map), encoded);
    safe_copy(ui->positions, sizeof(ui->positions), positions);
}

void ui_set_positions(ui_state_t *ui, const char *positions) {
    safe_copy(ui->positions, sizeof(ui->positions), positions);
}

void ui_set_game_over(ui_state_t *ui, const char *winner, const char *score, const char *scores) {
    ui->game_over = 1;
    snprintf(ui->game_result, sizeof(ui->game_result),
             "Vincitore: %s con %s celle | Punteggi: %s", winner, score, scores);
    safe_copy(ui->status, sizeof(ui->status), "partita terminata");
}

void ui_add_event(ui_state_t *ui, const char *fmt, ...) {
    va_list ap;
    int idx = ui->event_next;

    va_start(ap, fmt);
    vsnprintf(ui->events[idx], sizeof(ui->events[idx]), fmt, ap);
    va_end(ap);

    ui->event_next = (ui->event_next + 1) % UI_EVENTS_MAX;
    if (ui->event_count < UI_EVENTS_MAX) {
        ui->event_count++;
    }
}

void ui_render(const ui_state_t *ui, const char *input) {
    int tty = isatty(STDOUT_FILENO);
    char current_symbol = current_owner_symbol(ui);

    ui_clear_screen();

    print_rule();
    puts("CONQUISTA DEL TERRITORIO");
    print_rule();
    printf("Stato: %s | Server: %s | Utente: %s",
           ui->status, ui->server, ui->nickname);
    if (ui->position_known) {
        printf(" | Posizione: (%d,%d)", ui->x, ui->y);
    }
    putchar('\n');

    if (ui->game_over) {
        printf("Fine partita: %s\n", ui->game_result);
    }

    print_rule();
    puts("Comandi rapidi:");
    puts("  w/a/s/d  movimento        register <nick> <pass>");
    puts("  l/users  lista utenti     login <nick> <pass>");
    puts("  local    mappa locale     global mappa globale");
    puts("  h/help   guida            q/quit esci");
    print_legend(tty);

    print_rule();
    print_events(ui);

    print_rule();
    if (ui->local_valid) {
        print_map_block("Mappa locale", ui->local_w, ui->local_h, ui->local_map, tty, current_symbol);
    } else {
        puts("Mappa locale");
        puts("  disponibile dopo il login");
    }

    print_rule();
    if (ui->global_valid) {
        print_map_block("Mappa globale proprieta", ui->global_w, ui->global_h, ui->global_map, tty, current_symbol);
        printf("Giocatori: %s\n", ui->positions);
    } else {
        puts("Mappa globale");
        puts("  in attesa del primo aggiornamento periodico");
    }

    print_rule();
    printf("Prompt > %s", input != NULL ? input : "");
    if (!tty) {
        putchar('\n');
    }
    fflush(stdout);
}

void ui_finish(void) {
    if (isatty(STDOUT_FILENO)) {
        fputs("\033[0m\n", stdout);
        fflush(stdout);
    }
}
