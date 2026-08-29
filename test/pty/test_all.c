#define _GNU_SOURCE

#include <locale.h>
#include <string.h>
#include <stdlib.h>
#include <ncurses.h>

#include "unity_fixture.h"
#include "pty_test.h"
#include "dstr.h"
#include "curses_win.h"

void ncurses_cleanup(void) {
    endwin();
}

void run_all(void) {
    RUN_TEST_GROUP(shell);
}

int main(int argc, const char **argv) {
    setlocale(LC_ALL, "");
    initscr();
    curs_set(0);
    cbreak();
    noecho();
    start_color();
    use_default_colors();
    refresh();
    xatexit(ncurses_cleanup);

    init_curses_wins();
    harness_init();

    int tests_failed = UnityMain(argc, argv, run_all);

    add_lines(TEST_WIN, "\n");
    add_lines(TEST_WIN, unity_buf.c_str);

    char val_sum[4096];

    if (val_test_errs == 0) {
        snprintf(val_sum, 4096,
                "\nAll heap blocks were freed -- no leaks are possible");
    } else
        snprintf(val_sum, 4096, "\nValgrind %d Errors", val_test_errs);

    add_lines(TEST_WIN, val_sum);

    for (size_t i = 0; i < MAX_WIN; ++i) {
        curses_win *win = &wins[i];

        int start_offset = (int) win->lines.size - win->nlines;
        if (start_offset < 0)
            start_offset = 0;
        win->offset = start_offset;

        werase(win->win);
        render_lines(win);
        wrefresh(win->win);
    }

    if (keypad(stdscr, true) == ERR)
        curs_err("keypad");

    if (!mousemask(ALL_MOUSE_EVENTS, NULL))
        curs_err("mousemask");

    while (true) {
        int c = getch();

        if (c == 'q')
            break;

        if (c == KEY_MOUSE)
            while (handle_mouse_event())
                continue;
    }

    for (size_t i = 0; i < MAX_WIN; ++i)
        delwin(wins[i].win);
    endwin();

    _exit(tests_failed);
}
