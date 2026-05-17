#ifndef UI_H
#define UI_H

#include "common/protocol.h"

#include <stddef.h>

#define UI_MAP_MAX 512
#define UI_TEXT_MAX 256
#define UI_EVENTS_MAX 6
#define UI_INPUT_MAX 256

typedef struct {
    int connected;
    int authenticated;
    int position_known;
    int game_over;
    int show_help;
    int local_valid;
    int global_valid;
    int local_w;
    int local_h;
    int global_w;
    int global_h;
    int x;
    int y;
    char server[UI_TEXT_MAX];
    char nickname[NICK_MAX + 1];
    char status[UI_TEXT_MAX];
    char local_map[UI_MAP_MAX];
    char global_map[UI_MAP_MAX];
    char positions[UI_TEXT_MAX];
    char game_result[UI_TEXT_MAX];
    char events[UI_EVENTS_MAX][UI_TEXT_MAX];
    int event_count;
    int event_next;
} ui_state_t;

void ui_init(ui_state_t *ui, const char *host, const char *port);
void ui_set_connected(ui_state_t *ui, int connected);
void ui_set_user(ui_state_t *ui, const char *nickname);
void ui_set_position(ui_state_t *ui, int x, int y);
void ui_set_local_map(ui_state_t *ui, int w, int h, const char *encoded);
void ui_set_global_map(ui_state_t *ui, int w, int h, const char *encoded, const char *positions);
void ui_set_positions(ui_state_t *ui, const char *positions);
void ui_set_game_over(ui_state_t *ui, const char *winner, const char *score, const char *scores);
void ui_add_event(ui_state_t *ui, const char *fmt, ...);
void ui_render(const ui_state_t *ui, const char *input);
void ui_clear_screen(void);
void ui_finish(void);

#endif
