#ifndef GAME_H
#define GAME_H

#include "common/protocol.h"

#define MAP_W 30
#define MAP_H 15
#define LOCAL_VIEW_W 11
#define LOCAL_VIEW_H 11
// Struttura per rappresentare un giocatore, contenente il nickname, lo stato di connessione, il simbolo sulla mappa, la posizione e la mappa delle celle scoperte
typedef struct {
    char nickname[NICK_MAX + 1];
    int active; // 0 = disconnesso, 1 = connesso ma non ancora posizionato, 2 = connesso e posizionato
    int used; // 0 = slot non usato, 1 = slot usato (anche se il giocatore è disconnesso) dell'array dinamico di giocatori, usato per mantenere i dati dei giocatori anche dopo la disconnessione
    char symbol; // Simbolo del giocatore sulla mappa, ad esempio 'A', 'B', 'C', ...
    int x;
    int y;
    unsigned char discovered_walls[MAP_H][MAP_W]; // Mappa delle celle scoperte dal giocatore, 0 = sconosciuto, 1 = vuoto, 2 = muro
} player_t;

// Struttura per rappresentare lo stato del gioco, inclusi la mappa dei muri, la mappa dei proprietari delle celle, l'elenco dei giocatori attualmente nel gioco e il numero di giocatori
typedef struct {
    int wall[MAP_H][MAP_W]; // 0 = vuoto, 1 = muro
    int owner[MAP_H][MAP_W]; // -1 = nessuno, altrimenti indice del giocatore che occupa la cella
    player_t *players; // Array dinamico di giocatori attualmente nel gioco
    size_t player_count;
    size_t player_capacity;
} game_t;

void game_init(game_t *game);
void game_free(game_t *game);
int game_add_player(game_t *game, const char *nickname);
void game_remove_player(game_t *game, int player_id);
int game_find_player(const game_t *game, const char *nickname);
int game_move(game_t *game, int player_id, direction_t dir);
void game_reveal_around(game_t *game, int player_id);
void game_build_local_map(const game_t *game, int player_id, char *out, size_t out_size);
void game_build_global_map(const game_t *game, char *out, size_t out_size);
void game_build_positions(const game_t *game, char *out, size_t out_size);
void game_build_scores(const game_t *game, char *out, size_t out_size);
int game_winner(const game_t *game, char *nickname, size_t nickname_size, int *score);

#endif
