#include <assert.h>
#include <locale.h>
#include <ncurses.h>
#include <string.h>

#include "curses_debug.h"
#include "curses_xfuncs.h"
#include "pty_test.h"

int y, x;
int by, bx;
curses_win wins[4];
bool ncurses_initialized = false;
size_t test_i = 0;

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

static void ncurses_cleanup(void) {
    endwin();
}

static void init_ncurses(void) {
    if (ncurses_initialized) {
        if (isendwin())
            refresh();
        return;
    }

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

    if (refresh() == ERR)
        curs_err("refresh");

    xatexit(ncurses_cleanup);
    init_curses_wins();

    ncurses_initialized = true;
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

static void display_debug_wins(size_t test_i) {
    assert(all_tests.size > 0);

    init_ncurses();
    refresh_wins(test_i);

    while (true) {
        int c = getch();

        if (c == 'q' || c == '\n') {
            endwin();
            break;
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
