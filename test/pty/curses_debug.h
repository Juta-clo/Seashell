#ifndef CURSES_WIN_H
#define CURSES_WIN_H

#include <ncurses.h>

typedef enum {
    RAW_WIN,
    TEST_WIN,
    LOG_WIN,
    VAL_WIN,
    MAX_WIN
} win_names;

typedef enum {
    BRIGHT_WHITE = 8, /* ncurses uses 0-7 */
} curs_colors;

typedef enum {
    DEFAULT,
    BWHITE_RED,
    BWHITE_GREEN,
    BWHITE_YELLOW,
} color_pairs;

typedef struct {
    WINDOW *win;
    int offset;
    int nlines;
    int ncols;
    int y;
    int x;
} curses_win;

extern curses_win wins[MAX_WIN];

void start_debug_repl(void);

#endif
