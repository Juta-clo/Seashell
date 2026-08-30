#ifndef CURSES_WIN_H
#define CURSES_WIN_H

#include <ncurses.h>

#include "darr.h"

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

void init_curses_wins(void);
void print_window_borders(void);
void render_lines(curses_win *win, da_dstr *lines);
bool handle_mouse_event(void);
void clear_win_offsets(void);
void refresh_wins(size_t test_i);

#endif
