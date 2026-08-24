#include <errno.h>
#include <ncurses.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pty_test.h"
#include "llog.h"
#include "xfuncs.h"

int seashell_pipe[2];
curses_win wins[4];

XFATAL_HANDLER(pty_xfatal_func) {
    wcprintw(wins[HARNESS_WIN].win, CP_RED_CL, "ERROR");
    wprintw(wins[HARNESS_WIN].win, " %s: %s\n", XSYSNAME, strerror(errno));

    wrefresh(wins[HARNESS_WIN].win);
    exit(EXIT_FAILURE);
}

void curs_err(const char *msg) {
    wcprintw(wins[HARNESS_WIN].win, CP_RED_CL, "ERROR");
    wprintw(wins[HARNESS_WIN].win, " ncurses: %s\n", msg);

    wrefresh(wins[HARNESS_WIN].win);
    exit(EXIT_FAILURE);
}

LLOG_SINK(pty_test_sink) {
    int level_cp = CP_DEFAULT;
    char *lvl_str;

    switch (info->log_level) {
    case LLOG_INFO: lvl_str = "INFO";  level_cp = CP_CYAN_CL;   break;
    case LLOG_WARN: lvl_str = "WARN";  level_cp = CP_YELLOW_CL; break;
    case LLOG_ERR:  lvl_str = "ERROR"; level_cp = CP_RED_CL;    break;
    }

    xwcprintw(wins[HARNESS_WIN].win, level_cp, "%s", lvl_str);
    xwprintw(wins[HARNESS_WIN].win, " llog: %s:%d: %s\n",
            info->site->file, info->site->line, info->msg);

    wrefresh(wins[HARNESS_WIN].win);
}

void unity_output_char(int c) {
    xwaddch(wins[UNITY_WIN].win, c);
    xwrefresh(wins[UNITY_WIN].win);
}

/* assumes non blocking fd */
char *flush_stream(int fd, bool break_on_eio) {
#define FLUSH_STREAM_BS 4096

    char *out = NULL;
    size_t out_size = 0;

    while (true) {
        char pbuf[FLUSH_STREAM_BS];
        int num_read = read(fd, pbuf, FLUSH_STREAM_BS);

        if (num_read == -1) {
            if (errno == EAGAIN) {
                break;
            } else if (break_on_eio && errno == EIO) {
                break;
            } else {
                LOG_ERR("read: %m");
                exit(EXIT_FAILURE);
            }
        }

        out = xrealloc(out, out_size + num_read);
        memcpy(out + out_size, pbuf, num_read);
        out_size += num_read;
    }

    out = xrealloc(out, out_size + 1);
    out[out_size] = '\0';

    return out;
}

curses_win *init_wins(curses_win wins[4]) {
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
    return wins;
}
