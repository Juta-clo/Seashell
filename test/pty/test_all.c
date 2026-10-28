#include <assert.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <stdlib.h>
#include <ncurses.h>
#include <unistd.h>

#include "unity_fixture.h"
#include "t_harness.h"
#include "t_debug.h"

static bool mute_unity = false;
static bool skip_nls = false;

void unity_output_cb(char ch) {
    if (mute_unity)
        return;

    if (skip_nls) {
        if (ch == '\n')
            return;
        skip_nls = false;
    }

    putchar(ch);
}

void run_all(void) {
    mute_unity = true;

    RUN_TEST_GROUP(shell);
    RUN_TEST_GROUP(variable);
    RUN_TEST_GROUP(glob);
    RUN_TEST_GROUP(termios);
    RUN_TEST_GROUP(input);
    RUN_TEST_GROUP(resize);
    RUN_TEST_GROUP(config);

    mute_unity = false;
    skip_nls = true;
}

int main(int argc, const char **argv) {
    test_harness_init(argc, argv);
    UnityMain(argc, argv, run_all);
    test_harness_deinit();

    if (ptyh.tests.size == 0)
        exit(EXIT_SUCCESS);

    if (memcheck)
        print_val_sum();

    if ((!verbose && ptyh.pass) || script)
        exit((ptyh.pass) ? EXIT_SUCCESS : EXIT_FAILURE);

    start_debug_repl();
}
