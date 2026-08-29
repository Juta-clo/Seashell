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

typedef struct {
    WINDOW *win;
    da_dstr lines;
    int offset;
    int nlines;
    int ncols;
    int y;
    int x;
} curses_win;

extern curses_win wins[MAX_WIN];

void init_curses_wins(void);
void print_divider(const char *text);
void render_lines(curses_win *win);
bool handle_mouse_event(void);


#endif
