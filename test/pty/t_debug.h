#ifndef T_DEBUG_H
#define T_DEBUG_H

#include <ncurses.h>

#include "t_splitty.h"

typedef enum {
    WIN_RAW,
    WIN_TEST,
    WIN_LOG,
    WIN_VAL,
    WIN_MAX
} win_names;

typedef enum {
    GAP_L,
    GAP_R,
    GAP_T,
    GAP_B,
    GAP_J,
    GAP_MAX
} gap_names;

typedef enum {
    COLOR_BRIGHT_WHITE = 16, /* ascii indexes use 0-15 */
    COLOR_ORANGE
} curs_colors;

typedef enum {
    BRIGHT_WHITE_RED = 1,
    BRIGHT_WHITE_GREEN,
    BRIGHT_WHITE_YELLOW,
    BRIGHT_WHITE_ORANGE,
    ORANGE_CLEAR,
    MAGENTA_CLEAR,
    CYAN_CLEAR,
    RED_CLEAR,
    GREEN_CLEAR,
} color_pairs;

typedef struct {
    WINDOW* winp;
    sp_rect *rect;
    bool realized;
} curs_win;

void start_debug_repl(void);

#endif
