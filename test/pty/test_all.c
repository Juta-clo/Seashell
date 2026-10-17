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
    RUN_TEST_GROUP(input);
    RUN_TEST_GROUP(resize);
}

int main(int argc, const char **argv) {
    test_harness_init(argc, argv);
    UnityMain(argc, argv, run_all);
    test_harness_deinit();

    if (ptyh.tests.size == 0)
        exit(EXIT_SUCCESS);

    if (memcheck) {
        printf("\nValgrind memcheck -- ");
        if (ptyh.val_errs == 0)
            printf("\x1b[97;42mCLEAN\x1b[m\n");
        else
            printf("\x1b[97;41m%d TESTS DIRTY\x1b[m\n", ptyh.val_errs);
    }

    if ((!verbose && ptyh.fails == 0 && ptyh.val_errs == 0) || !script)
        exit((ptyh.fails == 0 && ptyh.val_errs == 0) ? 0 : 1);

    start_debug_repl();
}
