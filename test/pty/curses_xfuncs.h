#ifndef CURSES_XFUNCS_H
#define CURSES_XFUNCS_H

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
    do { \
        wattron(_win, COLOR_PAIR(cp_num)); \
        wprintw(_win, _fmt __VA_OPT__(,) __VA_ARGS__); \
        wattroff(_win, COLOR_PAIR(cp_num)); \
        wattron(_win, COLOR_PAIR(CP_DEFAULT)); \
    } while (false)

#define xwcprintw(_win, cp_num, _fmt, ...) \
    do { \
        xwattron(_win, COLOR_PAIR(cp_num)); \
        xwprintw(_win, _fmt __VA_OPT__(,) __VA_ARGS__); \
        xwattroff(_win, COLOR_PAIR(cp_num)); \
        xwattron(_win, COLOR_PAIR(CP_DEFAULT)); \
    } while (false)

#endif
