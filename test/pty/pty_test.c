#include <errno.h>
#include <ncurses.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "pty_test.h"
#include "dstr.h"
#include "llog.h"
#include "xfuncs.h"

int sh_pipe[2];
curses_win wins[4];
dstr unity_buf;
struct termios tio;

XFATAL_HANDLER(pty_xfatal_func) {
    endwin();
    fprintf(stderr, "ERROR %s: %s\n", XSYSNAME, strerror(XERRNO));
    _exit(EXIT_FAILURE);
}

void curs_err(const char *msg) {
    endwin();
    fprintf(stderr, "ERROR ncurses: %s\n", msg);
    _exit(EXIT_FAILURE);
}

LLOG_SINK(pty_test_sink) {
    char *lvl_str;
    switch (info->log_level) {
    case LLOG_INFO: lvl_str = "INFO"; break;
    case LLOG_WARN: lvl_str = "WARN"; break;
    case LLOG_ERR:  lvl_str = "ERROR"; break;
    }

    dstr *line = da_push(&wins[TEST_WIN].lines);
    dstr_init(line);
    dstr_printf(line, "%s llog: %s:%d: %s\n", lvl_str, info->site->file,
            info->site->line, info->msg);
}

void unity_output_char(int c) {
    dstr_push(&unity_buf, c);
}

void add_lines(int win_num, const char *str) {
    curses_win *win = &wins[win_num];

    if (strcmp(str, "") == 0)
        return;

    if (str[0] == '\n') {
        if (win->push_line)
            dstr_init(da_push(&wins[win_num].lines));

        win->push_line = true;

        if (strlen(str) == 1)
            return;
    }

    char *data = strdup(str);
    char *tok = strtok(data, "\n");
    dstr *line;

    while (tok != NULL) {
        if (win->push_line) {
            line = da_push(&wins[win_num].lines);
            dstr_init(line);
        } else {
            line = &win->lines.data[win->lines.size - 1];
            win->push_line = true;
        }

        dstrcat(line, tok);

        /* multiple '\n' are compressed so for programs
         * that for example just emit \n a bunch of times
         * the transcript will not be accurate. This is
         * pretty rare though */

        tok = strtok(NULL, "\n");
    }

    win->push_line = (str[strlen(str) - 1] == '\n');
    free(data);
}

int mev_to_win(MEVENT *mev) {
    if (mev->x >= wins[TEST_WIN].x) {
        if (mev->y >= wins[JOB_WIN].y)
            return JOB_WIN;
        else
            return TEST_WIN;
    } else {
        if (mev->y >= wins[JOB_WIN].y)
            return LOG_WIN;
        else
            return RAW_WIN;
    }
}

int start_timerfd(int ms) {
    int sec = ms / 1000;
    ms %= 1000;

    int ns = ms * 1000000;

    struct itimerspec its = {
        .it_value = {.tv_sec = sec, .tv_nsec = ns},
        .it_interval = {0}
    };

    int tfd = xtimerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    xtimerfd_settime(tfd, 0, &its, NULL);

    return tfd;
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

    for (size_t i = 0; i < MAX_WIN; ++i)
        wins[i].push_line = true;

    refresh();
    return wins;
}
