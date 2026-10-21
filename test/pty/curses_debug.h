#ifndef CURSES_WIN_H
#define CURSES_WIN_H

#include <ncurses.h>

#include "linc_tools.h"

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
    CBRIGHT_WHITE = 8, /* ncurses uses 0-7 */
    CORANGE = 9,
} curs_colors;

typedef enum {
    DEFAULT,
    BWHITE_RED,
    BWHITE_GREEN,
    BWHITE_YELLOW,
    ORANGE_CL,
    MAGENTA_CL,
    CYAN_CL,
    RED_CL,
    GREEN_CL,
} color_pairs;

typedef struct {
    WINDOW* winp;
    sp_rect *rect;
    bool realized;
} curs_win;

void start_debug_repl(void);

#endif
