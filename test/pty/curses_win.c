#include <ncurses.h>
#include <string.h>

#include "curses_win.h"
#include "curses_xfuncs.h"
#include "pty_test.h"

curses_win wins[4];

void init_curses_wins() {
    int y, x;
    getmaxyx(stdscr, y, x);

    int by = (y % 2 == 0) ? y / 2 : y / 2 + 1;
    int bx = (x % 2 == 0) ? x / 2 : x / 2 + 1;

    wins[0] = (curses_win){
        .nlines = by - 1, .ncols = bx - 1,
        .y      = 0,      .x     = 0
    };

    wins[1] = (curses_win){
        .nlines = by - 1, .ncols = x / 2,
        .y      = 0,      .x     = bx
    };

    wins[2] = (curses_win){
        .nlines = y / 2, .ncols = bx - 1,
        .y      = by,    .x     = 0
    };

    wins[3] = (curses_win){
        .nlines = y / 2, .ncols = x / 2,
        .y      = by,    .x     = bx
    };

    for (size_t i = 0; i < 4; ++i) {
        curses_win *cwin = &wins[i];

        cwin->win = xderwin(stdscr, cwin->nlines, cwin->ncols, cwin->y, cwin->x);
        xwrefresh(cwin->win);
        scrollok(wins[i].win, true);
    }

    for (int i = 0; i < y; ++i) {
        if (i == by - 1) {
            xmvaddch(i, bx - 1, ACS_PLUS);
        } else
            xmvaddch(i, bx - 1, ACS_VLINE);
    }

    for (int i = 0; i < x; ++i) {
        if (i == bx - 1) {
            xmvaddch(by - 1, i, ACS_PLUS);
        } else
            xmvaddch(by - 1, i, ACS_HLINE);
    }

    refresh();
}

void render_lines(curses_win *win) {
    for (int i = 0, wn = 0; i < win->nlines; ++i) {
        if (i + win->offset > (int) win->lines.size - 1)
            continue;

        if (i + win->offset < 0)
            continue;

        dstr *line = &win->lines.data[i - wn + win->offset];
        mvwaddnstr(win->win, i, 0, line->c_str, win->ncols - 1);

        char *p = line->c_str;

        while ((int) strlen(p) > win->ncols - 1 && i + wn + 1 < win->nlines) {
            p += win->ncols - 1;
            mvwaddnstr(win->win, ++i, 0, p, win->ncols - 1);
            ++wn;
        }
    }

    mvwprintw(win->win, 0, 0, "lines[%d]", win->offset);
}

bool handle_mouse_event(void) {
    MEVENT mev;
    if (getmouse(&mev) == ERR)
        return false;

    curses_win *win = &wins[mev_to_win(&mev)];

    if (mev.bstate & BUTTON4_PRESSED)
        --win->offset;

    if (mev.bstate & BUTTON5_PRESSED)
        ++win->offset;

    werase(win->win);
    render_lines(win);
    wrefresh(win->win);

    return true;
}
