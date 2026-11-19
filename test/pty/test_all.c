#include <assert.h>
#include <fcntl.h>
#include <linux/limits.h>
#include <stdlib.h>
#include <ncurses.h>
#include <unistd.h>

#include "unity_fixture.h"
#include "t_harness.h"
#include "t_debug.h"

#define DECLARE_GROUP(group) \
    extern void TEST_##group##_GROUP_RUNNER(void)

#define GROUP_ENTRY(group) \
    { #group, TEST_##group##_GROUP_RUNNER }

DECLARE_GROUP(shell);
DECLARE_GROUP(substitution);
DECLARE_GROUP(variable);
DECLARE_GROUP(glob);
DECLARE_GROUP(termios);
DECLARE_GROUP(input);
DECLARE_GROUP(highlight);
DECLARE_GROUP(completion);
DECLARE_GROUP(shadow);
DECLARE_GROUP(resize);
DECLARE_GROUP(config);

struct {
    const char *name;
    void (*run)(void);
} groups[] = {
    GROUP_ENTRY(shell),
    GROUP_ENTRY(substitution),
    GROUP_ENTRY(variable),
    GROUP_ENTRY(glob),
    GROUP_ENTRY(termios),
    GROUP_ENTRY(input),
    GROUP_ENTRY(highlight),
    GROUP_ENTRY(completion),
    GROUP_ENTRY(shadow),
    GROUP_ENTRY(resize),
    GROUP_ENTRY(config)
};

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

    for (size_t i = 0; i < ARRLEN(groups); ++i)
        groups[i].run();

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
