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

// Copia stringhe nello stato UI gestendo anche sorgenti NULL.
static void safe_copy(char *dst, size_t dst_size, const char *src)
{
    if (dst_size == 0)
    {
        return;
    }
    if (src == NULL)
    {
        src = "";
    }
    snprintf(dst, dst_size, "%s", src);
}

// Stampa una separazione costante fra sezioni della UI.
static void print_line(void)
{
    puts("============================================================");
}

// Pulisce lo schermo usando sequenze ANSI.
static void ui_clear_screen(void)
{
    fputs("\033[2J\033[3J\033[H", stdout);
    fflush(stdout);
}

// Calcola un hash semplice per assegnare colori stabili agli identificatori.
static unsigned int hash_text(const char *s)
{
    unsigned int h = 5381;
    while (s != NULL && *s != '\0')
    {
        h = ((h << 5) + h) ^ (unsigned char)*s;
        ++s;
    }
    return h;
}

// Restituisce il colore ANSI associato a un identificatore giocatore.
static const char *owner_color(const char *symbol)
{
    static const char *palette[] = {
        "\033[1;31m",
        "\033[1;34m",
        "\033[1;33m",
        "\033[1;35m",
        "\033[1;36m",
        "\033[1;32m"};
    return palette[hash_text(symbol) % (sizeof(palette) / sizeof(palette[0]))];
}

// Distingue le celle proprieta dalle celle speciali del protocollo.
static int is_owner_symbol(const char *cell)
{
    return cell != NULL &&
           strcmp(cell, ".") != 0 &&
           strcmp(cell, "#") != 0 &&
           strcmp(cell, "@") != 0 &&
           strcmp(cell, "~") != 0 &&
           cell[0] != '\0';
}

// Converte una cella codificata nel glifo mostrato a terminale.
static const char *cell_glyph(const char *cell)
{
    if (strcmp(cell, "@") == 0)
    {
        return CELL_CURRENT_PLAYER;
    }
    if (strcmp(cell, "#") == 0)
    {
        return CELL_WALL;
    }
    if (strcmp(cell, ".") == 0)
    {
        return CELL_FREE;
    }
    if (strcmp(cell, "~") == 0)
    {
        return " ";
    }
    if (is_owner_symbol(cell))
    {
        return CELL_OWNED;
    }
    return NULL;
}

// Stampa una cella applicando il colore se stdout e un terminale.
static void print_colored_cell(const char *cell, int color_enabled, const char *current_symbol)
{
    const char *glyph = cell_glyph(cell);
    const char *color;

    if (glyph == NULL)
    {
        fputs("?", stdout);
        return;
    }

    if (!color_enabled)
    {
        printf("%s", glyph);
        return;
    }

    if (strcmp(cell, "@") == 0)
    {
        color = is_owner_symbol(current_symbol) ? owner_color(current_symbol) : ANSI_PLAYER;
        printf("%s%s%s", color, glyph, ANSI_RESET);
    }
    else if (strcmp(cell, "#") == 0)
    {
        printf("%s%s%s", ANSI_WALL, glyph, ANSI_RESET);
    }
    else if (strcmp(cell, ".") == 0 || strcmp(cell, "~") == 0)
    {
        printf("%s%s%s", ANSI_DIM, glyph, ANSI_RESET);
    }
    else if (is_owner_symbol(cell))
    {
        printf("%s%s%s", owner_color(cell), glyph, ANSI_RESET);
    }
    else
    {
        fputs("?", stdout);
    }
}

// Mostra la legenda dei simboli usati nelle mappe.
static void print_legend(int color_enabled)
{
    printf("Legenda: ");
    print_colored_cell("@", color_enabled, "P0");
    printf(" tu  ");
    print_colored_cell("#", color_enabled, "P0");
    printf(" muro  ");
    print_colored_cell(".", color_enabled, "P0");
    printf(" libero  ");
    print_colored_cell("P0", color_enabled, "P0");
    printf("/");
    print_colored_cell("P1", color_enabled, "P0");
    printf("/... territori\n");
}

// Decodifica e stampa una mappa con celle separate da virgole e righe da '/'.
static void print_map_block(const char *title, int w, int h, const char *encoded, int color_enabled, const char *current_symbol)
{
    int rows = 0;
    int col = 0;
    const char *p = encoded;
    char cell[UI_SYMBOL_MAX];
    size_t cell_len = 0;

    printf("%s (%dx%d)\n", title, w, h);
    if (encoded == NULL || encoded[0] == '\0')
    {
        puts("  non ancora disponibile");
        return;
    }

    while (rows < h)
    {
        if (*p == ',' || *p == '/' || *p == '\0')
        {
            cell[cell_len] = '\0';
            if (cell_len > 0)
            {
                print_colored_cell(cell, color_enabled, current_symbol);
                col++;
                cell_len = 0;
            }
            if (*p == '/' || col == w || *p == '\0')
            {
                putchar('\n');
                rows++;
                col = 0;
            }
            if (*p == '\0') {
                break;
            }
        }
        else if (cell_len + 1 < sizeof(cell))
        {
            cell[cell_len++] = *p;
        }
        ++p;
    }
    if (col != 0)
    {
        putchar('\n');
    }
}

// Stampa gli ultimi eventi salvati nel ring buffer.
static void print_events(const ui_state_t *ui)
{
    int start;
    int count;
    int i;

    puts("Ultimi eventi:");
    if (ui->event_count == 0)
    {
        puts("  - In attesa di eventi dal server");
        return;
    }

    count = ui->event_count < UI_EVENTS_MAX ? ui->event_count : UI_EVENTS_MAX;
    start = ui->event_count < UI_EVENTS_MAX ? 0 : ui->event_next;
    for (i = 0; i < count; ++i)
    {
        int idx = (start + i) % UI_EVENTS_MAX;
        printf("  - %s\n", ui->events[idx]);
    }
}

// Inizializza lo stato UI prima dell'autenticazione.
void ui_init(ui_state_t *ui, const char *host, const char *port)
{
    memset(ui, 0, sizeof(*ui));
    snprintf(ui->server, sizeof(ui->server), "%s:%s", host, port);
    safe_copy(ui->nickname, sizeof(ui->nickname), "-");
    safe_copy(ui->positions, sizeof(ui->positions), "-");
    safe_copy(ui->status, sizeof(ui->status), "connessione in corso");
    safe_copy(ui->player_symbol, sizeof(ui->player_symbol), "P0");
}

// Aggiorna lo stato di connessione visualizzato.
void ui_set_connected(ui_state_t *ui, int connected)
{
    ui->connected = connected;
    safe_copy(ui->status, sizeof(ui->status), connected ? "connesso" : "disconnesso");
}

// Salva nickname e identificatore assegnati dopo il login.
void ui_set_user(ui_state_t *ui, const char *nickname, const char *player_symbol)
{
    ui->authenticated = 1;
    safe_copy(ui->nickname, sizeof(ui->nickname), nickname);
    safe_copy(ui->player_symbol, sizeof(ui->player_symbol), player_symbol);
    safe_copy(ui->status, sizeof(ui->status), "autenticato");
}

// Aggiorna la posizione nota del giocatore.
void ui_set_position(ui_state_t *ui, int x, int y)
{
    ui->position_known = 1;
    ui->x = x;
    ui->y = y;
}

// Aggiorna la mappa locale ricevuta dal server.
void ui_set_local_map(ui_state_t *ui, int w, int h, const char *encoded)
{
    ui->local_valid = 1;
    ui->local_w = w;
    ui->local_h = h;
    safe_copy(ui->local_map, sizeof(ui->local_map), encoded);
}

// Aggiorna mappa globale e posizioni pubbliche.
void ui_set_global_map(ui_state_t *ui, int w, int h, const char *encoded, const char *positions)
{
    ui->global_valid = 1;
    ui->global_w = w;
    ui->global_h = h;
    safe_copy(ui->global_map, sizeof(ui->global_map), encoded);
    safe_copy(ui->positions, sizeof(ui->positions), positions);
}

// Aggiorna solo la lista posizioni/utenti.
void ui_set_positions(ui_state_t *ui, const char *positions)
{
    safe_copy(ui->positions, sizeof(ui->positions), positions);
}

// Memorizza il risultato finale della partita.
void ui_set_game_over(ui_state_t *ui, const char *winner, const char *score, const char *scores)
{
    ui->game_over = 1;
    snprintf(ui->game_result, sizeof(ui->game_result),
             "Vincitore: %s con %s celle | Punteggi: %s", winner, score, scores);
    safe_copy(ui->status, sizeof(ui->status), "partita terminata");
}

// Aggiunge un messaggio al ring buffer degli eventi.
void ui_add_event(ui_state_t *ui, const char *fmt, ...)
{
    va_list ap;
    int idx = ui->event_next;

    va_start(ap, fmt);
    vsnprintf(ui->events[idx], sizeof(ui->events[idx]), fmt, ap);
    va_end(ap);

    ui->event_next = (ui->event_next + 1) % UI_EVENTS_MAX;
    if (ui->event_count < UI_EVENTS_MAX)
    {
        ui->event_count++;
    }
}

// Renderizza l'intera interfaccia testuale.
void ui_render(const ui_state_t *ui, const char *input)
{
    int tty = isatty(STDOUT_FILENO);

    ui_clear_screen();

    print_line();
    puts("CONQUISTA DEL TERRITORIO");
    print_line();
    printf("Stato: %s | Server: %s | Utente: %s",
           ui->status, ui->server, ui->nickname);
    if (ui->position_known)
    {
        printf(" | Posizione: (%d,%d)", ui->x, ui->y);
    }
    putchar('\n');

    if (ui->game_over)
    {
        printf("Fine partita: %s\n", ui->game_result);
    }

    print_line();
    puts("Comandi rapidi:");
    puts("  w/a/s/d  movimento        register <nick> <pass>");
    puts("  l/users  lista utenti     login <nick> <pass>");
    puts("  local    mappa locale     global mappa globale");
    puts("  h/help   guida            q/quit esci");
    print_legend(tty);

    print_line();
    print_events(ui);

    print_line();
    if (ui->local_valid)
    {
        print_map_block("Mappa locale", ui->local_w, ui->local_h, ui->local_map, tty, ui->player_symbol);
    }
    else
    {
        puts("Mappa locale");
        puts("  disponibile dopo il login");
    }

    print_line();
    if (ui->global_valid)
    {
        print_map_block("Mappa globale proprieta", ui->global_w, ui->global_h, ui->global_map, tty, ui->player_symbol);
        printf("Giocatori: %s\n", ui->positions);
    }
    else
    {
        puts("Mappa globale");
        puts("  in attesa del primo aggiornamento periodico");
    }

    print_line();
    printf("Prompt > %s", input != NULL ? input : "");
    if (!tty)
    {
        putchar('\n');
    }
    fflush(stdout);
}

// Ripristina lo stile terminale alla chiusura del client.
void ui_finish(void)
{
    if (isatty(STDOUT_FILENO))
    {
        fputs("\033[0m\n", stdout);
        fflush(stdout);
    }
}
