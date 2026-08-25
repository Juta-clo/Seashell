#define _GNU_SOURCE

#include <stdlib.h>
#include <locale.h>
#include <ncurses.h>

#include "unity_fixture.h"
#include "xfuncs.h"
#include "llog.h"
#include "pty_test.h"
#include "dstr.h"

static void test_init(void) {
    set_xfatal_handler(pty_xfatal_func);
    llog_set_sink(pty_test_sink);
    xpipe2(sh_pipe, O_NONBLOCK);
    dstr_init(&unity_buf);

    xtcgetattr(STDERR_FILENO, &tio);
    tio.c_lflag &= ~ECHO; /* don't echo, non-interactive */
    tio.c_oflag &= ~OPOST; /* no post processing, \r\n -> \n */
}

static void ncurses_cleanup(void) {
    getch();
    for (size_t i = 0; i < 4; ++i)
        delwin(wins[i].win);
    endwin();
}

static void ncurses_setup(void) {
    setlocale(LC_ALL, "");
    initscr();
    curs_set(0);
    cbreak();
    noecho();
    start_color();
    use_default_colors();
    init_pair(CP_RED_CL, COLOR_RED, COLOR_BLACK);
    init_pair(CP_GREEN_CL, COLOR_GREEN, COLOR_BLACK);
    init_pair(CP_CYAN_CL, COLOR_CYAN, COLOR_BLACK);
    init_pair(CP_MAGENTA_CL, COLOR_MAGENTA, COLOR_BLACK);
    init_pair(CP_YELLOW_CL, COLOR_YELLOW, COLOR_BLACK);

    refresh();
    xatexit(ncurses_cleanup);
}

void run_all(void) {
    RUN_TEST_GROUP(prompt);
}

int main(void) {
    ncurses_setup();
    test_init();
    init_wins(wins);

    int argc = 2;
    const char *argv[] = { "test_pty", "-s", NULL };

    // int argc = 4;
    // const char *argv[] = { "test_pty", "-s", "-n", "bg_job_done_with_fg_job", NULL };

    int tests_failed = UnityMain(argc, argv, run_all);

    if (tests_failed > 0) {
        xwprintw(wins[TEST_WIN].win, "\n");
        for (int y = 0 ; y < wins[TEST_WIN].ncols - 1; ++y)
            xwcprintw(wins[TEST_WIN].win, CP_MAGENTA_CL, "-");
        xwcprintw(wins[TEST_WIN].win, CP_RED_CL, "\n%s", unity_buf.c_str);
        for (int y = 0 ; y < wins[TEST_WIN].ncols - 1; ++y)
            xwcprintw(wins[TEST_WIN].win, CP_MAGENTA_CL, "-");

    } else {
        xwprintw(wins[TEST_WIN].win, "\n");
        for (int y = 0 ; y < wins[TEST_WIN].ncols - 1; ++y)
            xwcprintw(wins[TEST_WIN].win, CP_MAGENTA_CL, "-");
        xwcprintw(wins[TEST_WIN].win, CP_GREEN_CL,
                "\nOK - Ran %ld tests\nPassed %ld - Failed 0 - Ignored %ld\n",
                Unity.NumberOfTests, Unity.NumberOfTests - Unity.TestIgnores,
                Unity.TestIgnores);
        for (int y = 0 ; y < wins[TEST_WIN].ncols - 1; ++y)
            xwcprintw(wins[TEST_WIN].win, CP_MAGENTA_CL, "-");
        xwprintw(wins[TEST_WIN].win, "\n");
    }

    xwrefresh(wins[TEST_WIN].win);

    if (keypad(stdscr, true) == ERR)
        curs_err("keypad");

    if (!mousemask(ALL_MOUSE_EVENTS, NULL))
        curs_err("mousemask");

    while (true) {
        int c = getch();

        if (c == KEY_MOUSE) {
            MEVENT mev;

            if (getmouse(&mev) == OK) {
                curses_win *w = &wins[mev_to_win(&mev)];

                if (mev.bstate & BUTTON4_PRESSED) {
                    wscrl(w->win, -1);
                }

                if (mev.bstate & BUTTON5_PRESSED) {
                    wscrl(w->win, 1);
                }

                for (size_t i = 0; i < MAX_WIN; ++i)
                    xwrefresh(wins[i].win);
            } else
                curs_err("getmouse");
        } else if (c == 'q')
            break;
    }

    for (size_t i = 0; i < 4; ++i)
        delwin(wins[i].win);
    endwin();

    _exit(tests_failed);
}
