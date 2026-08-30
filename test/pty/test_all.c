#define _GNU_SOURCE

#include <assert.h>
#include <locale.h>
#include <string.h>
#include <stdlib.h>
#include <ncurses.h>

#include "input.h"
#include "unity_fixture.h"
#include "pty_test.h"
#include "curses_win.h"

bool ncurses_initialized = false;

void ncurses_cleanup(void) {
    endwin();
}

void init_ncurses(void) {
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

size_t find_next_failing_test(size_t test_i) {
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

size_t find_prev_failing_test(size_t test_i) {
    bool failed_test_found = false;

    for (; test_i != 0; --test_i) {
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

void display_debug_wins(size_t test_i) {
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

void run_all(void) {
    RUN_TEST_GROUP(shell);
}

int main(int argc, const char **argv) {
    for (const char **arg = argv; *arg != NULL; ++arg) {
        if (strcmp("-v", *arg) == 0)
            verbose_mode = true;
        else if (strcmp("-m", *arg) == 0 || strcmp("--memcheck", *arg) == 0)
            run_valgrind = true;
    }

    harness_init();
    UnityMain(argc, argv, run_all);
    if (all_tests.size == 0)
        exit(EXIT_SUCCESS);

    size_t test_i = 0;

    if (run_valgrind) {
        printf("\nValgrind memcheck -- ");

        if (val_test_fail_count() == 0) {
            printf("\x1b[97;42mCLEAN\x1b[m\n");
        } else {
            printf("\x1b[97;41m%d TESTS DIRTY\x1b[m\n", val_test_fail_count());
        }
    }

    if (!verbose_mode) {
        size_t next = find_next_failing_test(0);
        if (next == (size_t)-1)
            exit(EXIT_SUCCESS);

        test_i = next;
    }

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

        } else if (strcmp("exit", line) == 0) {
            return EXIT_SUCCESS;

        } else if (strncmp("shell ", line, 6) == 0) {
            if (system(line + 6) == -1) {
                perror("system");
                return EXIT_FAILURE;
            }

        } else if (strcmp("", line) == 0) /* newline */
            display_debug_wins(test_i);

        else {
            printf("Unrecognized command\n");
        }
    }
}
