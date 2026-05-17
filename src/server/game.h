#ifndef GAME_H
#define GAME_H

#include "common/protocol.h"

#define MAP_W 30
#define MAP_H 15
#define LOCAL_VIEW_W 11
#define LOCAL_VIEW_H 11
#define PLAYER_SYMBOL_MAX 32

// Stato persistente di un giocatore. Lo slot resta associato al nickname
// anche dopo la disconnessione, cosi la proprieta delle celle rimane stabile.
typedef struct {
    char nickname[NICK_MAX + 1];
    int active;
    int used;
    char symbol[PLAYER_SYMBOL_MAX];
    int x;
    int y;
    unsigned char discovered_walls[MAP_H][MAP_W];
} player_t;

typedef struct {
    int wall[MAP_H][MAP_W];
    int owner[MAP_H][MAP_W];
    player_t *players;
    size_t player_count;
    size_t player_capacity;
} game_t;

void game_init(game_t *game);
void game_free(game_t *game);
int game_add_player(game_t *game, const char *nickname);
void game_remove_player(game_t *game, int player_id);
int game_find_player(const game_t *game, const char *nickname);
int game_move(game_t *game, int player_id, direction_t dir);
void game_build_local_map(const game_t *game, int player_id, char *out, size_t out_size);
void game_build_global_map(const game_t *game, char *out, size_t out_size);
void game_build_positions(const game_t *game, char *out, size_t out_size);
void game_build_scores(const game_t *game, char *out, size_t out_size);
int game_winner(const game_t *game, char *nickname, size_t nickname_size, int *score);

#endif
