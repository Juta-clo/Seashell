#include <assert.h>
#include <linux/limits.h>
#include <ncurses.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "linc_tools.h"
#include "t_harness.h"
#include "t_utils.h"

#define TEST_BIN_DIR "./test/pty/test_bins"

bool verbose = false;
bool memcheck = false;
bool script = false;

pty_harness ptyh = {0};
pty_test *test;

static void test_open_log_files(void) {
    char log_path[] = "/tmp/seashell/shlog-XXXXXX";
    char log_raw_out_path[] = "/tmp/seashell/shlog-raw-out-XXXXXX";
    char log_raw_in_path[] = "/tmp/seashell/shlog-raw-in-XXXXXX";

    ptyh.log_file = new_tmp_file(log_path);
    ptyh.log_raw_out = new_tmp_file(log_raw_out_path);
    ptyh.log_raw_in = new_tmp_file(log_raw_in_path);

    test_relink(log_path, "/tmp/shlog-latest");
    test_relink(log_raw_out_path, "/tmp/shlog-latest.raw.out");
    test_relink(log_raw_in_path, "/tmp/shlog-latest.raw.in");
}

static void process_args(int argc, const char *const *argv) {
    for (int i = 0; i < argc; ++i) {
        if (strcmp("-v", argv[i]) == 0 || strcmp("--verbose", argv[i]) == 0) {
            verbose = true;
            hds_append(ptyh.cmd_str, "--verbose");
        } else if (strcmp("-m", argv[i]) == 0 ||
                   strcmp("--memcheck", argv[i]) == 0) {
            memcheck = true;
            hds_append(ptyh.cmd_str, "--memcheck");
        } else if (strcmp("-s", argv[i]) == 0 ||
                   strcmp("--script", argv[i]) == 0) {
            script = true;
            hds_append(ptyh.cmd_str, "--script");
        } else if (strcmp("-h", argv[i]) == 0) {
            printf("Custom harness flags:\n"
                    "  -m          Memcheck: run under valgrind\n"
                    "  -v          Verbose: Also enter repl when no tests fail\n"
                    "  -s          Script: no repl ever\n\n"
            );
        } else {
            hds_append(ptyh.cmd_str, argv[i]);
        }

        hds_append(ptyh.cmd_str, " ");
    }
}

static void create_exec_argv(void) {
    if (pipe2(ptyh.sh_p, O_NONBLOCK) == -1)
        xtest_fatal("pipe2: %m");
    if (pipe2(ptyh.val_p, O_NONBLOCK) == -1)
        xtest_fatal("pipe2: %m");

    if (memcheck) {
        char *val_cmd[] = {
            "valgrind",
                "--vgdb=no",
                "--leak-check=full",
                "--errors-for-leak-kinds=definite,indirect",
                "--show-leak-kinds=definite,indirect",
                "--error-exitcode=99", /*seashell shouldn't return that*/
                "--track-fds=bad", /*this is awesome - bugged with forkpty tho*/
                "--modify-fds=high",
                /* this is bugged with sigsuspend. See bugzilla#526386 */
                "--enable-debuginfod=no",
                NULL
        };

        for (char **arg = val_cmd; memcheck && *arg != NULL; ++arg)
            hds_append(*hda_push(ptyh.exec_argv, nullptr), *arg);

        /* append lfd arg */

        char val_lfd_arg[ARG_MAX];
        if (snprintf(val_lfd_arg, ARG_MAX, "--log-fd=%d", ptyh.val_p[1]) < 0)
            xtest_fatal("snprintf: %m");
        hds_append(*hda_push(ptyh.exec_argv, nullptr), val_lfd_arg);
    }

    char *sh_cmd[] = {
        "./seashell",
            "--prompt=simple",
            NULL
    };

    for (char **arg = sh_cmd; *arg != NULL; ++arg)
        hds_append(*hda_push(ptyh.exec_argv, nullptr), *arg);

    /* append lfd arg */
    char sh_lfd_arg[ARG_MAX];
    if (snprintf(sh_lfd_arg, ARG_MAX, "--logfd=%d", ptyh.sh_p[1]) < 0)
        xtest_fatal("snprintf: %m");
    hds_append(*hda_push(ptyh.exec_argv, nullptr), sh_lfd_arg);

    hda_push(ptyh.exec_argv, nullptr);
}

void test_harness_init(int argc, const char *const *argv) {
    process_args(argc, argv);
    create_exec_argv();

    if (mkdir("/tmp/seashell", 0700) == -1 && errno != EEXIST)
        xtest_fatal("mkdir: %m");

    test_open_log_files();

    char *path = getenv("PATH");
    char *new = nullptr;

    if (!path) {
        hds_append(new, TEST_BIN_DIR);
    } else {
        hds_append(new, path);
        hds_append(new, ":");
        hds_append(new, TEST_BIN_DIR);
    }

    if (setenv("PATH", new, 1) == -1)
        xtest_fatal("setenv: %m");

    hds_free(new);

    /* unibi_from_env uses TERM */
    if (setenv("TERM", "xterm-256color", 1) == -1)
        xtest_fatal("setenv: %m");
}

void test_harness_deinit(void) {
    if (ptyh.log_file)
        fclose(ptyh.log_file);
    if (ptyh.log_raw_out)
        fclose(ptyh.log_raw_out);
    if (ptyh.log_raw_in)
        fclose(ptyh.log_raw_in);

    if (ptyh.fails == 0 && ptyh.val_errs == 0)
        ptyh.pass = true;
}

void print_val_sum(void) {
    printf("\nValgrind status -- ");
    if (ptyh.val_errs == 0)
        printf("\x1b[97;42mCLEAN\x1b[m\n");
    else
        printf("\x1b[97;41m%d TESTS DIRTY\x1b[m\n", ptyh.val_errs);
}

void test_resize_term(int row, int col) {
    if (row < 0 || col < 0)
        xtest_fatal("test_resize_term: bad row/col");

    vterm_set_size(test->vt.vp, row, col);
    test->vt.rows = row;
    test->vt.cols = col;

    struct winsize ws = { .ws_row = row, .ws_col = col };
    if (ioctl(test->mfd, TIOCSWINSZ, &ws) == -1)
        xtest_fatal("ioctl");
}

int test_poll(int tfd) {
    enum pfds {
        POLL_TFD, POLL_MFD, POLL_SH,
        POLL_VAL, POLL_NFDS
    };

    struct pollfd pollfds[POLL_NFDS] = {
        [POLL_TFD] = { .fd = tfd,           .events = POLLIN },
        [POLL_MFD] = { .fd = test->mfd,     .events = POLLIN },
        [POLL_SH]  = { .fd = ptyh.sh_p[0],  .events = POLLIN },
        [POLL_VAL] = { .fd = ptyh.val_p[0], .events = POLLIN }
    };

    while (true) {
        if (poll(pollfds, POLL_NFDS, -1) == -1)
            xtest_fatal("poll: %m");

        if (pollfds[POLL_TFD].revents & POLLIN)
            return -1;

        if (pollfds[POLL_MFD].revents & POLLIN) {
            char *data = flush_pty(test->mfd);
            dstrcat(&test->pty_out_data, data);
            vt_write(data);
            free(data);
            return 0;
        }

        if (pollfds[POLL_SH].revents & POLLIN) {
            char *data = flush_pipe(ptyh.sh_p[0]);
            dstrcat(&test->shell_data, data);
            free(data);
        }

        if (pollfds[POLL_VAL].revents & POLLIN) {
            char *data = flush_pipe(ptyh.val_p[0]);
            dstrcat(&test->val_data, data);
            free(data);
        }
    }
}

void test_pump_ms(int ms) {
    if (ms == 0) {
        char *data = flush_pty(test->mfd);
        dstrcat(&test->pty_out_data, data);
        vt_write(data);
        free(data);

        data = flush_pipe(ptyh.sh_p[0]);
        dstrcat(&test->shell_data, data);
        free(data);

        data = flush_pipe(ptyh.val_p[0]);
        dstrcat(&test->val_data, data);
        free(data);

        return;
    }

    int tfd = start_timerfd(ms);

    while (test_poll(tfd) != -1)
        continue;

    if (close(tfd) == -1)
        xtest_fatal("close: %m");
}
