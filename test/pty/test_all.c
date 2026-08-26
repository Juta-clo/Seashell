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

static void render_lines(curses_win *win) {
    for (int i = 0; i < win->nlines; ++i) {
        if (i + win->offset > (int) win->lines.size - 1)
            continue;

        if (i + win->offset < 0)
            continue;

        dstr *line = &win->lines.data[i + win->offset];
        mvwaddnstr(win->win, i, 0, line->c_str, win->ncols);
    }

    mvwprintw(win->win, 0, 0, "lines[%d]", win->offset);
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

    werase(win->win);
    render_lines(win);
    wrefresh(win->win);

    return true;
}

int main(int argc, const char **argv) {
    ncurses_setup();
    test_init();
    init_wins(wins);

    // int argc = 2;
    // const char *argv[] = { "./pty_test",  NULL };

    int tests_failed = UnityMain(argc, argv, run_all);

    add_lines(TEST_WIN, unity_buf.c_str);

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
