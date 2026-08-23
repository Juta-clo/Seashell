#define _GNU_SOURCE

#include <locale.h>
#include <ncurses.h>

#include "unity_fixture.h"
#include "xfuncs.h"
#include "llog.h"
#include "pty_test.h"

static void test_init(void) {
    set_xfatal_handler(pty_xfatal_func);
    llog_set_sink(pty_test_sink);
    xpipe2(seashell_pipe, O_NONBLOCK);
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

    int tests_failed = UnityMain(argc, argv, run_all);

    return tests_failed;
}
