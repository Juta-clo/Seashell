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

char **get_static_args(void) {
    static char log_arr[ARG_MAX];
    static char *out[2];

    if (pipe2(ptyh.sh_p, O_NONBLOCK) == -1)
        xtest_fatal("pipe2: %m");

    out[0] = log_arr;
    out[1] = nullptr;

    if (snprintf(out[0], ARG_MAX, "--log-fd=%d", ptyh.sh_p[1]) < 0)
        xtest_fatal("snprintf: %m");

    return out;
}

void run_all(void) {
    mute_unity = true;

    RUN_TEST_GROUP(shell);
    RUN_TEST_GROUP(variable);
    RUN_TEST_GROUP(glob);
    RUN_TEST_GROUP(termios);
    RUN_TEST_GROUP(input);
    RUN_TEST_GROUP(resize);

    mute_unity = false;
    skip_nls = true;
}

int main(int argc, const char **argv) {
    test_harness_init(argc, argv);
    static_args_cb = get_static_args;

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
