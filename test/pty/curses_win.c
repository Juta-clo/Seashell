#include <ncurses.h>
#include <string.h>

#include "curses_win.h"
#include "curses_xfuncs.h"
#include "pty_test.h"

int y, x;
int by, bx;
curses_win wins[4];

void init_curses_wins() {
    getmaxyx(stdscr, y, x);
    y -= 1;

    by = (y % 2 == 0) ? y / 2 : y / 2 + 1;
    bx = (x % 2 == 0) ? x / 2 : x / 2 + 1;

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
}

void print_window_borders(void) {
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

void render_lines(curses_win *win, da_dstr *lines) {
    if (lines->size == 0) {
        mvwaddnstr(win->win, 0, 0, "[No data]", win->ncols - 1);
        return;
    }

    for (int i = 0, wn = 0; i < win->nlines; ++i) {
        if (i - wn + win->offset > (int) lines->size - 1)
            continue;

        if (i - wn + win->offset < 0)
            continue;

        dstr *line = &lines->data[i - wn + win->offset];
        mvwaddnstr(win->win, i, 0, line->c_str, win->ncols - 1);

        char *p = line->c_str;

        while ((int) strlen(p) > win->ncols - 1 && i + 1 < win->nlines) {
            p += win->ncols - 1;
            mvwaddnstr(win->win, ++i, 0, p, win->ncols - 1);
            ++wn;
        }
    }
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

    return true;
}

void clear_win_offsets(void) {
    for (int i = 0; i < MAX_WIN; ++i)
        wins[i].offset = 0;
}

void refresh_wins(size_t test_i) {
    erase();
    print_window_borders();

    if (!all_tests.data[test_i].success)
        attron(COLOR_PAIR(BWHITE_RED));
    else if (all_tests.data[test_i].val_err)
        attron(COLOR_PAIR(BWHITE_YELLOW));
    else
        attron(COLOR_PAIR(BWHITE_GREEN));

    mvprintw(y, 0, "[%ld/%ld]: %s",
            test_i + 1, all_tests.size, all_tests.data[test_i].test_name);

    if (!all_tests.data[test_i].success)
        attroff(COLOR_PAIR(BWHITE_RED));
    else if (all_tests.data[test_i].val_err)
        attroff(COLOR_PAIR(BWHITE_YELLOW));
    else
        attroff(COLOR_PAIR(BWHITE_GREEN));

    refresh();

    for (int win_i = 0; win_i < MAX_WIN; ++win_i) {
        werase(wins[win_i].win);

        if (win_i == RAW_WIN) {
            render_lines(&wins[win_i], &all_tests.data[test_i].raw_lines);
        } else if (win_i == LOG_WIN) {
            render_lines(&wins[win_i], &all_tests.data[test_i].shell_log_lines);
        } else if (win_i == VAL_WIN) {
            render_lines(&wins[win_i], &all_tests.data[test_i].val_lines);
        } else if (win_i == TEST_WIN) {
            render_lines(&wins[win_i], &all_tests.data[test_i].test_lines);
        }

        wrefresh(wins[win_i].win);
    }
}
