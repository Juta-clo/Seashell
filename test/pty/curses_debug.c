#define _GNU_SOURCE

#include <errno.h>
#include <sys/poll.h>
#include <signal.h>
#include <assert.h>
#include <locale.h>
#include <ncurses.h>
#include <string.h>

#include "curses_debug.h"
#include "curses_xfuncs.h"
#include "xfuncs.h"
#include "pty_test.h"

#define NFDS 1

int y, x;
int by, bx;
curses_win wins[4];
bool ncurses_initialized = false;
size_t test_i = 0;
sigset_t og_set;

struct pollfd pfds[NFDS] = {
    (struct pollfd){ .events = POLLIN, .fd = STDIN_FILENO }
};

static volatile sig_atomic_t caught_sigwinch = 0;

static void sigwinch_handler(int _) {
    caught_sigwinch = true;
}

static void init_curses_wins() {
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

void process_signals(void) {
    if (!caught_sigwinch)
        return;

    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1)
        curs_err("ioctl");

    if (ncurses_initialized)
        if (resize_term(ws.ws_row, ws.ws_col) == ERR)
            curs_err("resize_term");

    if (!isendwin()) {
        for (size_t i = 0; i < MAX_WIN; ++i)
            delwin(wins[i].win);

        init_curses_wins();

        if (refresh() == ERR)
            curs_err("refresh");
    }

    caught_sigwinch = false;
}

static char *get_line(void) {
#define LINE_BUF 8192
    static char input_line[LINE_BUF];

    if (!fgets(input_line, LINE_BUF, stdin))
        curs_err("fgets failed");

    if (feof(stdout)) {
        LOG_INFO("eof, exiting");
        exit(EXIT_SUCCESS);
    }

    if (input_line[strlen(input_line) - 1] == '\n')
        input_line[strlen(input_line) - 1 ] = '\0';

    return input_line;
}

static int mev_to_win(MEVENT *mev) {
    if (mev->x >= wins[TEST_WIN].x) {
        if (mev->y >= wins[VAL_WIN].y)
            return VAL_WIN;
        else
            return TEST_WIN;
    } else {
        if (mev->y >= wins[VAL_WIN].y)
            return LOG_WIN;
        else
            return RAW_WIN;
    }
}

static void print_window_borders(void) {
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

static void clear_win_offsets(void) {
    for (int i = 0; i < MAX_WIN; ++i)
        wins[i].offset = 0;
}

static void ncurses_cleanup(void) {
    endwin();
}

void suspend_ncurses(void) {
    for (size_t i = 0; i < MAX_WIN; ++i)
        delwin(wins[i].win);

    endwin();
}

void resume_ncurses(void) {
    if (!isendwin())
        curs_err("resume_ncurses: not suspended");

    init_curses_wins();

    if (refresh() == ERR)
        curs_err("refresh");
}

static void init_ncurses(void) {
    if (ncurses_initialized) {
        resume_ncurses();
        return;
    }

    /* Set signals before initscr so ncurses doesn't set its own */
    /* block sigwinch and save original set for use with ppoll */
    sigset_t new_set;
    xsigprocmask(SIG_SETMASK, NULL, &new_set);
    xsigaddset(&new_set, SIGWINCH);
    xsigprocmask(SIG_SETMASK, &new_set, &og_set);
    /* set sigwinch disposition */
    struct sigaction sa = { .sa_handler = sigwinch_handler };
    xsigemptyset(&sa.sa_mask);
    xsigaction(SIGWINCH, &sa, NULL);

    if (!setlocale(LC_ALL, "")) {
        perror("setlocale");
        exit(EXIT_FAILURE);
    }

    /* terminal opts */
    if (!initscr())
        curs_err("initscr");
    if (curs_set(0) == ERR)
        curs_err("curs_set");
    if (cbreak() == ERR)
        curs_err("cbreak");
    if (noecho() == ERR)
        curs_err("noecho");

    /* color */
    if (start_color() == ERR)
        curs_err("noecho");
    if (use_default_colors() == ERR)
        curs_err("use_default_colors");
    if (init_color(BRIGHT_WHITE, 984, 945, 780) == ERR)
        curs_err("init_color");
    if (init_pair(BWHITE_RED, BRIGHT_WHITE, COLOR_RED) == ERR)
        curs_err("init_pair");
    if (init_pair(BWHITE_GREEN, BRIGHT_WHITE, COLOR_GREEN) == ERR)
        curs_err("init_pair");
    if (init_pair(BWHITE_YELLOW, BRIGHT_WHITE, COLOR_YELLOW) == ERR)
        curs_err("init_pair");

    /* input */
    if (keypad(stdscr, true) == ERR)
        curs_err("keypad");
    if (!mousemask(ALL_MOUSE_EVENTS, NULL))
        curs_err("mousemask");

    xatexit(ncurses_cleanup);
    ncurses_initialized = true;

    init_curses_wins();

    if (refresh() == ERR)
        curs_err("refresh");
}

static size_t find_next_failing_test(size_t test_i) {
    bool failed_test_found = false;

    for (; test_i < all_tests.size; ++test_i) {
        pty_test *test = &all_tests.data[test_i];

        if (!test->success || test->val_err) {
            failed_test_found = true;
            break;
        }
    }

    if (!failed_test_found) {
        return (size_t) -1;
    } else
        return test_i;
}

static size_t find_prev_failing_test(size_t test_i) {
    bool failed_test_found = false;

    for (; test_i != 0; --test_i) {
        pty_test *test = &all_tests.data[test_i];

        if (!test->success || test->val_err) {
            failed_test_found = true;
            break;
        }
    }

    if (test_i == 0) {
        pty_test *test = &all_tests.data[test_i];

        if (!test->success || test->val_err)
            failed_test_found = true;
    }

    if (!failed_test_found) {
        return (size_t) -1;
    } else
        return test_i;
}

static void render_lines(curses_win *win, da_dstr *lines) {
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

static bool handle_mouse_event(void) {
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

static void refresh_wins(size_t test_i) {
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

void render_snap(vt_snap *snap, WINDOW *win) {
    WINDOW *border_win = newwin(y, x, 0, 0);
    wborder(border_win, 0, 0, 0, 0, 0, 0, 0 ,0);

    for (int row = 0; row < (int) snap->lines.size; ++row) {
        char *line = snap->lines.data[row];
        int col = 0;

        for (char *c = line; *c != '\0'; ++c, ++col) {
            if (snap->curs_pos.row == row && snap->curs_pos.col == col)
                wattron(win, A_REVERSE);

            mvwaddch(win, row, col, *c);

            wattroff(win, A_REVERSE);
        }
    }

    xwrefresh(border_win);
    xwrefresh(win);
    delwin(border_win);
}

void view_vterm(size_t test_i) {
    pty_test *test = &all_tests.data[test_i];

    WINDOW *win = newwin(y - 2, x - 2, 1, 1);
    wclear(win);

    if (test->vterm.rows > y - 1 || test->vterm.cols > x - 1) {
        mvwprintw(win, 0, 0, "[vterm too large to display]");
        getch();
        goto done;
    }

    size_t i = 0;
    while (true) {
        erase();
        attron(A_BOLD);
        mvprintw(y, 0, "VTerm Snapshot [%ld/%ld]", i + 1, test->snaps.size);
        attroff(A_BOLD);
        refresh();

        render_snap(&test->snaps.data[i], win);

        int c = getch();

        if (c == 'v')
            break;

        if (c == 'l' && i < test->snaps.size - 1)
            ++i;

        if (c == 'h' && i > 0)
            --i;
    }

done:
    wrefresh(win);
    delwin(win);
    refresh_wins(test_i);
}

static void display_debug_wins(size_t test_i) {
    assert(all_tests.size > 0);

    init_ncurses();
    refresh_wins(test_i);

    while (true) {
        int ready = ppoll(pfds, NFDS, NULL, &og_set);

        if (ready == -1) {
            if (errno != EINTR)
                curs_err("ppoll");

            process_signals();
            refresh_wins(test_i);

            continue;
        }

        int c = getch();

        if (c == 'q' || c == '\n') {
            suspend_ncurses();
            break;
        }

        if (c == 'v') {
            view_vterm(test_i);
        }

        if (c == KEY_MOUSE)
            while (handle_mouse_event())
                continue;

        if (c == 'l') {
            if (test_i >= all_tests.size - 1)
                continue;

            ++test_i;

            if (!verbose_mode) {
                size_t next = find_next_failing_test(test_i);
                if (next != (size_t) -1) {
                    test_i = next;
                } else
                    --test_i;
            }

            clear_win_offsets();
        }

        if (c == 'h') {
            if (test_i == 0)
                continue;

            --test_i;

            if (!verbose_mode) {
                size_t next = find_prev_failing_test(test_i);
                if (next != (size_t) -1) {
                    test_i = next;
                } else {
                    ++test_i;
                }
            }

            clear_win_offsets();
        }

        refresh_wins(test_i);
    }
}

void start_debug_repl(void) {
    test_i = (verbose_mode) ? 0 : find_next_failing_test(0);
    if (test_i == (size_t)-1)
        return;

    printf("\n");

    while (true) {
        printf("> ");
        fflush(stdout);

        int ready = ppoll(pfds, NFDS, NULL, &og_set);

        if (ready == -1) {
            if (errno != EINTR)
                curs_err("ppoll");

            process_signals();
            continue;
        }

        char *line = get_line(); /* probably should generalize this */

        if (strcmp("verbose", line) == 0) {
            if (verbose_mode) {
                size_t next = find_next_failing_test(0);
                if (next == (size_t)-1) {
                    printf("No failed tests\n");

                } else {
                    test_i = next;
                    verbose_mode = false;
                    printf("disabled - wont show passed tests\n");
                }
            } else {
                test_i = 0;
                verbose_mode = true;
                printf("enabled - will show passed tests\n");
            }

        } else if (strcmp("h", line) == 0) {
            printf("<Enter> - Toggle debug view\n"
                   "shell - Shell command\n"
                   "verbose - toggle verbose mode\n"
                   "exit - Exit to terminal\n");

        } else if (strncmp("shell ", line, 6) == 0) {
            if (system(line + 6) == -1) {
                perror("system");
                exit(EXIT_FAILURE);
            }

        } else if (strcmp("exit", line) == 0) {
            return;

        } else if (strcmp("", line) == 0) /* newline */
            display_debug_wins(test_i);

        else {
            printf("Unrecognized command\n");
        }
    }
}
