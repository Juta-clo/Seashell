#ifndef PTY_TEST_H
#define PTY_TEST_H

#include <ncurses.h>

#include "llog.h"
#include "xfuncs.h"

#define PTY_RBUF 100000

#define UNITY_WIN 0
#define HARNESS_WIN 1
#define RAW_WIN 2
#define LOG_WIN 3

#define CP_DEFAULT 0
#define CP_RED_CL 1
#define CP_GREEN_CL 2
#define CP_CYAN_CL 3
#define CP_YELLOW_CL 4
#define CP_MAGENTA_CL 5

#define PIPE_READ(_pipe, ...) \
    read(_pipe[0], __VA_ARGS__)

#define PIPE_WRITE(_pipe, ...) \
    write(_pipe[1], __VA_ARGS__)

#define xwprintw(_win, _fmt, ...) \
    ({ \
        int rv = wprintw(_win, _fmt __VA_OPT__(,) __VA_ARGS__); \
        if (rv == ERR) \
            curs_err("wprintw"); \
        rv; \
    })

#define xmvaddch(y, x, ch) \
    ({ \
        int rv = mvaddch(y, x, ch); \
        if (rv == ERR) \
            curs_err("mvaddch"); \
        rv; \
    })

#define xwaddch(_win, _ch) \
    ({ \
        int rv = waddch(_win, _ch); \
        if (rv == ERR) \
            curs_err("waddch"); \
        rv; \
    })

#define xwrefresh(_win) \
    ({ \
        int rv = wrefresh(_win); \
        if (rv == ERR) \
            curs_err("wrefresh"); \
        rv; \
    })

#define xderwin(_orig, _nlines, _ncols, _begin_y, _begin_x) \
    ({ \
        WINDOW *rv = derwin(_orig, _nlines, _ncols, _begin_y, _begin_x); \
        if (!rv) \
            curs_err("derwin"); \
        rv; \
    })

#define xwattron(_win, cp) \
    ({ \
        int rv = wattron(_win, cp); \
        if (rv == ERR) \
            curs_err("wattron"); \
        rv; \
    })

#define xwattroff(_win, cp) \
    ({ \
        int rv = wattroff(_win, cp); \
        if (rv == ERR) \
            curs_err("wattroff"); \
        rv; \
    })

#define xwerase(_win) \
    ({ \
        int rv = werase(_win); \
        if (rv == ERR) \
            curs_err("werase"); \
        rv; \
    })

#define wcprintw(_win, cp_num, _fmt, ...) \
    wattron(_win, COLOR_PAIR(cp_num)); \
    wprintw(_win, _fmt __VA_OPT__(,) __VA_ARGS__); \
    wattroff(_win, COLOR_PAIR(cp_num)); \
    wattron(_win, COLOR_PAIR(CP_DEFAULT));

#define xwcprintw(_win, cp_num, _fmt, ...) \
    xwattron(_win, COLOR_PAIR(cp_num)); \
    xwprintw(_win, _fmt __VA_OPT__(,) __VA_ARGS__); \
    xwattroff(_win, COLOR_PAIR(cp_num)); \
    xwattron(_win, COLOR_PAIR(CP_DEFAULT));

typedef struct {
    WINDOW *win;
    int nlines;
    int ncols;
    int y;
    int x;
} curses_win;

typedef struct {
    size_t len;
    char buf[PTY_RBUF];
    size_t n;
} pty_io;

extern int seashell_pipe[2];
extern curses_win wins[4];

XFATAL_HANDLER(pty_xfatal_func);
void curs_err(const char *msg);
LLOG_SINK(pty_test_sink);
void unity_output_char(int c);

char *flush_pipe(int pipe[2]);
curses_win *init_wins(curses_win wins[4]);

#endif
