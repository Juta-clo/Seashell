#define _GNU_SOURCE

#include <sys/poll.h>
#include <assert.h>
#include <locale.h>
#include <ncurses.h>
#include <string.h>

#include "splitty.h"
#include "curses_debug.h"
#include "curses_xfuncs.h"
#include "xfuncs.h"
#include "pty_test.h"

#define NFDS 1

bool ncurses_initialized = false;

static void ncurses_cleanup(void) {
    endwin();
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

static void get_test_next(size_t *start_i) {
    assert(*start_i < all_tests.size);

    if (*start_i == all_tests.size - 1)
        return;

    if (verbose_mode) {
        ++*start_i;
        return;
    }

    for (size_t i = *start_i + 1; i < all_tests.size; ++i) {
        pty_test *test = &all_tests.data[i];

        if (!test->success || test->val_err) {
            *start_i = i;
            break;
        }
    }
}

static void get_test_prev(size_t *start_i) {
    assert(*start_i < all_tests.size);

    if (*start_i == 0)
        return;

    if (verbose_mode) {
        --*start_i;
        return;
    }

    for (size_t i = *start_i - 1; (int) i >= 0; --i) {
        pty_test *test = &all_tests.data[i];

        if (!test->success || test->val_err) {
            *start_i = i;
            return;
        }
    }
}

/* SPLITTY HANDLERS */
int split_init(sp_node *node) {
    if (!node->user_data)
        node->user_data = xmalloc(sizeof(curs_win));

    sp_rect rect = node->rect;
    curs_win *win = node->user_data;

    win->winp = NULL;
    win->realized = false;

    if (rect.rows <= 0 || rect.cols <= 0)
        return -1;

    WINDOW *winp = newwin(rect.rows, rect.cols, rect.y, rect.x);
    if (!winp)
        return -1;

    if (scrollok(winp, true) == ERR)
        curs_err("scrollok");

    win->winp = winp;
    win->rect = &node->rect;
    win->realized = true;

    return 0;
}

void split_resize(sp_node *node, uint32_t _) {
    curs_win *win = node->user_data;

    win->realized = false;

    if (node->rect.rows <= 0 || node->rect.cols <= 0)
        return;

    if (wresize(win->winp, node->rect.rows, node->rect.cols) == ERR)
        return;

    if (mvwin(win->winp, node->rect.y, node->rect.x) == ERR)
        return;

    win->realized = true;
}

void split_free(sp_node *node) {
    curs_win *win = node->user_data;

    if (win && delwin(win->winp) == ERR)
        curs_err("delwin");

    free(win);
}

/* NEW UI */
void init_curses(void) {
    if (ncurses_initialized)
        return;

    if (!setlocale(LC_ALL, "")) {
        perror("setlocale");
        exit(EXIT_FAILURE);
    }

    if (!initscr()) {
        fprintf(stderr, "initscr failed!\n");
        exit(EXIT_FAILURE);
    }

    /* options */
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

    if (refresh() == ERR)
        curs_err("refresh");

    ncurses_initialized = true;
}

/* UTILS */
sp_rect get_quad_rect(void) {
    int y, x;
    getmaxyx(stdscr, y, x);
    return (sp_rect){ .rows = y - 1, .cols = x, .y = 0, .x = 0 };
}

sp_rect get_info_bar_rect(void) {
    int y, x;
    getmaxyx(stdscr, y, x);
    return (sp_rect){ .rows = 1, .cols = x, .y = y - 1, .x = 0 };
}

chtype *str_to_chtype_arr(const char *s, size_t n, chtype attrs) {
    chtype *out = xmalloc(sizeof(chtype) * (n + 1));

    size_t i = 0;

    for (; i < n; ++i) {
        if (s[i] == '\0')
            break;
        out[i] = (chtype){s[i]} | attrs;
    }

    out[i] = (chtype){0};

    return out;
}

__attribute__ ((__format__(printf, 6, 7)))
void rnprintf(WINDOW *winp, int y, int x, size_t n, chtype attrs,
        const char *fmt, ...)
{
    va_list va;
    char *str;

    va_start(va, fmt);
    if (vasprintf(&str, fmt, va) == -1)
        curs_err("vasprintf");
    va_end(va);

    chtype *chstr = str_to_chtype_arr(str, n, attrs);

    if (mvwaddchnstr(winp, y, x, chstr, n) == ERR)
        curs_err("mvwaddchnstr");

    free(str);
    free(chstr);
}

/* RENDER FUNCS */
void render_quad_borders(curs_win *gaps[GAP_MAX]) {
    for (size_t i = 0; i < GAP_MAX; ++i) {
        curs_win *gap = gaps[i];
        sp_rect rect = *gap->rect;

        if (!gap->realized)
            continue;

        if (werase(gap->winp) == ERR)
            curs_err("werase");

        for (int row = 0; row < rect.rows; ++row) {
            for (int col = 0; col < rect.cols; ++col) {
                int ch;

                if (i == GAP_J) {
                    ch = ACS_PLUS;
                } else if (i == GAP_L || i == GAP_R) {
                    ch = ACS_HLINE;
                } else if (i == GAP_T || i == GAP_B)
                    ch = ACS_VLINE;

                if (mvwaddchnstr(gap->winp, row, col, &(chtype){ch}, 1) == ERR)
                    curs_err("mvwaddch - '+'");
            }
        }

        xwrefresh(gap->winp);
    }
}

void render_test_bar(curs_win *test_bar, size_t test_i) {
    if (!test_bar->realized)
        return;

    pty_test *test = &all_tests.data[test_i];
    char test_bar_str[BUFSIZ];
    int cp_n;

    if (werase(test_bar->winp) == ERR)
        curs_err("werase");

    int n = snprintf(test_bar_str, sizeof(test_bar_str),
            "[%zu/%zu]: %s", test_i + 1, all_tests.size, test->test_name);

    if (n < 0)
        curs_err("snprintf");

    if (n >= (int)sizeof(test_bar_str))
        n = sizeof(test_bar_str) - 1;

    if (n > test_bar->rect->cols)
        n = test_bar->rect->cols;

    if      (!test->success) cp_n = BWHITE_RED;
    else if (test->val_err)  cp_n = BWHITE_YELLOW;
    else                     cp_n = BWHITE_GREEN;

    rnprintf(test_bar->winp, 0, 0, n, COLOR_PAIR(cp_n), "%s", test_bar_str);
    xwrefresh(test_bar->winp);
}

void render_vterm_snap(curs_win *win, int trows, int tcols, vt_snap *snap) {
    if (!win->realized)
        return;

    if (werase(win->winp) == ERR)
        curs_err("werase");

    sp_rect rect = *win->rect;
    int avail_rows = MIN(rect.rows, trows);
    int avail_cols = MIN(rect.cols, tcols);

    for (int row = 0; row < avail_rows; ++row) {
        char *line = snap->lines.data[row];
        rnprintf(win->winp, row, 0, avail_cols, 0, "%s", line);
    }

    int curs_row = snap->curs_pos.row;
    int curs_col = snap->curs_pos.col;

    if (curs_row < avail_rows && curs_col < avail_cols)
        mvwchgat(win->winp, curs_row, curs_col, 1, A_REVERSE, 0, NULL);

    xwrefresh(win->winp);
}

static void render_debug_win(curs_win *win, da_dstr *lines, int offset) {
    if (!win->realized)
        return;

    if (werase(win->winp) == ERR)
        curs_err("werase");

    if (lines->size == 0) {
        rnprintf(win->winp, 0, 0, win->rect->cols, 0, "[No data]");
        xwrefresh(win->winp);
        return;
    }

    for (int i = 0, wn = 0; i < win->rect->rows; ++i) {
        if (i - wn + offset > (int)lines->size - 1)
            continue;

        if (i - wn + offset < 0)
            continue;

        dstr *line = &lines->data[i - wn + offset];
        rnprintf(win->winp, i, 0, win->rect->cols, 0, "%s", line->c_str);
        char *p = line->c_str;

        while ((int) strlen(p) > win->rect->cols - 1 && i + 1 < win->rect->rows) {
            p += win->rect->cols - 1;
            rnprintf(win->winp, ++i, 0, win->rect->cols, 0, "%s", p);
            ++wn;
        }
    }

    xwrefresh(win->winp);
}

static int lookup_win(curs_win *wins[WIN_MAX], int y, int x) {
    if (x >= wins[WIN_TEST]->rect->x) {
        if (y >= wins[WIN_VAL]->rect->y) {
            return WIN_VAL;
        } else
            return WIN_TEST;
    } else {
        if (y >= wins[WIN_VAL]->rect->y) {
            return WIN_LOG;
        } else
            return WIN_RAW;
    }
}

static void process_mevs(curs_win *wins[WIN_MAX], int offsets[WIN_MAX]) {
    MEVENT mev;

    while (getmouse(&mev) != ERR) {
        int win_i = lookup_win(wins, mev.y, mev.x);

        if (mev.bstate & BUTTON4_PRESSED)
            --offsets[win_i];

        if (mev.bstate & BUTTON5_PRESSED)
            ++offsets[win_i];
    }
}

void new_curses_ui() {
    init_curses();

    /* initialize main quad display */
    sp_node *nodes[SP_QUAD_CHILD_MAX];

    for (int i = 0; i < SP_QUAD_CHILD_MAX; ++i)
        nodes[i] = sp_new_leaf(split_init, split_resize, split_free);

    sp_node *quad = sp_new_quad((sp_quad_config) {
        .ratio_y = 0.5F, .ratio_x = 0.5F, .quad_gap = 1,
        .tl = nodes[SP_QUAD_TL], .tr = nodes[SP_QUAD_TR],
        .bl = nodes[SP_QUAD_BL], .br = nodes[SP_QUAD_BR],
        .gl = nodes[SP_QUAD_GL], .gr = nodes[SP_QUAD_GR],
        .gt = nodes[SP_QUAD_GT], .gb = nodes[SP_QUAD_GB],
        .gj = nodes[SP_QUAD_GJ],
    });

    sp_refresh(quad, get_quad_rect());

    curs_win *wins[WIN_MAX] = {
        [WIN_RAW] = nodes[SP_QUAD_TL]->user_data,
        [WIN_TEST] = nodes[SP_QUAD_TR]->user_data,
        [WIN_LOG] = nodes[SP_QUAD_BL]->user_data,
        [WIN_VAL] = nodes[SP_QUAD_BR]->user_data
    };

    curs_win *gaps[GAP_MAX] = {
        [GAP_L] = nodes[SP_QUAD_GL]->user_data,
        [GAP_R] = nodes[SP_QUAD_GR]->user_data,
        [GAP_T] = nodes[SP_QUAD_GT]->user_data,
        [GAP_B] = nodes[SP_QUAD_GB]->user_data,
        [GAP_J] = nodes[SP_QUAD_GJ]->user_data
    };

    /* initialize test bar */
    sp_node *bottom_bar = sp_new_leaf(split_init, split_resize, split_free);
    sp_refresh(bottom_bar, get_info_bar_rect());
    curs_win *test_bar = bottom_bar->user_data;

    /* get test data to display */
    size_t test_i = 0;
    if (!verbose_mode)
        if (all_tests.data[test_i].success && !all_tests.data[test_i].val_err)
            get_test_next(&test_i);

    int offsets[WIN_MAX] = {0};
    size_t snap_i = all_tests.data[test_i].snaps.size - 1;

    while (true) {
        sp_refresh(quad, get_quad_rect());
        sp_refresh(bottom_bar, get_info_bar_rect());

        erase();
        refresh();

        pty_test *test = &all_tests.data[test_i];

        render_quad_borders(gaps);
        render_test_bar(test_bar, test_i);
        render_debug_win(wins[WIN_TEST], &test->test_lines, offsets[WIN_TEST]);
        render_debug_win(wins[WIN_LOG], &test->shell_log_lines, offsets[WIN_LOG]);
        render_debug_win(wins[WIN_VAL], &test->val_lines, offsets[WIN_VAL]);
        render_vterm_snap(wins[WIN_RAW], test->vterm.rows, test->vterm.cols,
                &test->snaps.data[snap_i]);

        int ch = getch();

        if (ch == ERR)
            curs_err("getch");

        if (ch == 'q')
            break;

        if (ch == KEY_MOUSE)
            process_mevs(wins, offsets);

        if (ch == 'h') {
            for (size_t i = 0; i < WIN_MAX; ++i)
                offsets[i] = 0;
            get_test_prev(&test_i);
            snap_i = all_tests.data[test_i].snaps.size - 1;
        }

        if (ch == 'l') {
            for (size_t i = 0; i < WIN_MAX; ++i)
                offsets[i] = 0;
            get_test_next(&test_i);
            snap_i = all_tests.data[test_i].snaps.size - 1;
        }

        if (ch == 'j')
            snap_i = MIN(test->snaps.size - 1, snap_i + 1);

        if (ch == 'k')
            snap_i = MAX(0, (int) snap_i - 1);

        if (ch == KEY_LEFT)
            if (quad->ratio_x > 0.05F)
                quad->ratio_x -= 0.05F;

        if (ch == KEY_RIGHT)
            if (quad->ratio_x < 0.95F)
                quad->ratio_x += 0.05F;

        if (ch == KEY_UP)
            if (quad->ratio_y > 0.05F)
                quad->ratio_y -= 0.05F;

        if (ch == KEY_DOWN)
            if (quad->ratio_y < 0.95F)
                quad->ratio_y += 0.05F;
    }

    sp_free(quad);
    sp_free(bottom_bar);
    endwin();
}

void start_debug_repl(void) {
    printf("\n");
    while (true) {
        printf("> ");
        fflush(stdout);

        char *line = get_line();

        if (strcmp("h", line) == 0) {
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
            new_curses_ui();

        else {
            printf("Unrecognized command\n");
        }
    }
}
