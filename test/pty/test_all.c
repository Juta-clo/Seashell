#include <assert.h>
#include <stdlib.h>
#include <ncurses.h>

#include "unity_fixture.h"
#include "pty_test.h"
#include "curses_debug.h"

void run_all(void) {
    RUN_TEST_GROUP(shell);
    RUN_TEST_GROUP(variable);
    RUN_TEST_GROUP(glob);
    RUN_TEST_GROUP(termios);
}

int main(int argc, const char **argv) {
    harness_init(argc, argv);
    UnityMain(argc, argv, run_all);

    if (all_tests.size == 0)
        exit(EXIT_SUCCESS);

    if (run_valgrind)
        print_valgrind_overview();

    if (!verbose_mode && test_fail_count() == 0)
        exit(EXIT_SUCCESS);

    start_debug_repl();
}
