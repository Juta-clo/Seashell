#include <assert.h>
#include <pty.h>
#include <signal.h>
#include <sys/wait.h>
#include <time.h>

#include "t_assert.h"
#include "unity_fixture.h" // IWYU pragma: keep
#include "t_fixture.h"
#include "t_harness.h"
#include "t_utils.h"

static struct timespec tstart, tend;

static void get_diff_msg(void) {
    if (test->vt.rows != test->vt_act.rows ||
        test->vt.rows != test->vt_exp.rows)
        xtest_fatal("unexpected row count");

    bool curs_dirty;
    da_int dirty_lines = vt_ch_dirty(&test->vt_exp, &test->vt_act, &curs_dirty);

    if (curs_dirty) {
        linc_string_appendf(&test->diff_msg, "CURSOR -- DIRTY\n");
    } else {
        linc_string_appendf(&test->diff_msg, "CURSOR -- CLEAN\n");
    }

    linc_string_appendf(&test->diff_msg, "expected [%d,%d] actual [%d,%d]\n",
            test->vt_exp.crow, test->vt_exp.ccol,
            test->vt_act.crow, test->vt_act.ccol);

    if (dirty_lines.size != 0) {
        linc_string_appendf(&test->diff_msg, "VTERM -- DIRTY\n");
        linc_string_appendf(&test->diff_msg, "---expected\n");
        linc_string_appendf(&test->diff_msg, "+++actual\n");

        for (size_t i = 0; i < dirty_lines.size; ++i) {
            int ind = dirty_lines.data[i];
            char *exp = test->vt_exp.lines.data[ind];
            char *act = test->vt_act.lines.data[ind];

            linc_string_appendf(&test->diff_msg, "%d -%s\n", ind, exp);
            linc_string_appendf(&test->diff_msg, "%d +%s\n", ind, act);
        }
    } else {
        linc_string_appendf(&test->diff_msg, "VTERM -- CLEAN\n");
    }

    linc_array_free(&dirty_lines);
}

static void log_test(void) {
    *linc_array_push(&test->snaps) = vt_get_snap();

    fprintf(ptyh.log_file, "%s\n", ptyh.cmd_str);
    fprintf(ptyh.log_file, "%s:FAIL:%s:%d:%s\n",
            test->test_name, test->test_file, test->fail_line,
            test->fail_msg.cstr);

    fprintf(ptyh.log_file, "Test took %lf seconds\nSeashell %s\n",
        test->duration, test->exit_msg.cstr);

    for (char **arg = test->argv; *arg != nullptr; ++arg)
        fprintf(ptyh.log_file, "%s ", *arg);

    fprintf(ptyh.log_file, "\n");

    if (!test->fail_exec) {
        fprintf(ptyh.log_file, "===DIFF(` marks empty cell)===\n");
        fprintf(ptyh.log_file, "%s", test->diff_msg.cstr);

        fprintf(ptyh.log_file, "===SHELL LOGS===\n");
        for (size_t i = 0; i < test->shlog_lines.size; ++i) {
            linc_string *line = &test->shlog_lines.data[i];
            fprintf(ptyh.log_file, "%s", line->cstr);
            if (i != test->shlog_lines.size - 1)
                fprintf(ptyh.log_file, "\n");
        }

        fprintf(ptyh.log_file, "===VALGRIND===\n");
        if (test->val_lines.size == 0)
            fprintf(ptyh.log_file, "[memcheck not used]\n");

        if (!test->val_err) {
            fprintf(ptyh.log_file, "CLEAN\n");
        } else {
            for (size_t i = 0; i < test->val_lines.size; ++i) {
                linc_string *line = &test->val_lines.data[i];
                fprintf(ptyh.log_file, "%s\n", line->cstr);
            }
        }

        convert_special_chars(&test->pty_out_data);
        convert_special_chars(&test->pty_in_data);
        fprintf(ptyh.log_raw_out, "%s\n", test->pty_out_data.cstr);
        fprintf(ptyh.log_raw_in, "%s\n", test->pty_in_data.cstr);
    }
}

#define INFO_SEP "\x1b[1;38;2;255;133;54m:\x1b[m"

static void print_test_pass(void) {
    if (script) {
        printf("%s:%s:PASS\n",
                test->test_name, test->test_file);
    } else {
        printf("%s" INFO_SEP "%s" INFO_SEP "\x1b[97;42mPASS\x1b[m\n",
                test->test_name, test->test_file);
    }
}

static void print_test_fail(void) {
    if (script) {
        printf("%s:%s:",
                test->test_name, test->test_file);

        if (test->fail_line != 0)
            printf("%d:", test->fail_line);

        printf("%s:FAIL\n", test->fail_msg.cstr);
    } else {
        printf("%s" INFO_SEP "%s" INFO_SEP,
                test->test_name, test->test_file);

        if (test->fail_line != 0)
            printf("\x1b[1;38;2;50;200;150m%d\x1b[m" INFO_SEP, test->fail_line);

        printf("%s" INFO_SEP "\x1b[97;41mFAIL\x1b[m\n", test->fail_msg.cstr);
    }
}

static void test_shutdown_prgm(void) {
    /* so the shell can write any final data to the mfd before
    *  exiting. If we don't sleep the transcript might be cut off.
    *  Also if the shell sends a dsr request allow the request to
    *  be passed to vterm so seashell doesn't fail becuase it did
    *  not receive the request. */

    if (kill(test->cpid, SIGHUP) == -1)
        xtest_fatal("kill: %m");

    int checks = (memcheck) ? 1000 : 100;
    int rv, stat, i;

    for (i = 0; i < checks; ++i) {
        if ((rv = waitpid(test->cpid, &stat, WNOHANG)) == -1)
            xtest_fatal("waitpid: %m");

        if (rv != 0)
            break;

        test_pump_ms(1);
    }

    if (i == checks) {
        if (kill(test->cpid, SIGKILL) == -1)
            xtest_fatal("kill: %m");

        if (waitpid(test->cpid, &stat, 0) == -1)
            xtest_fatal("waitpid: %m");

        Unity.CurrentTestFailed = true;
        test_push_err("waitpid timed out");
    }

    test_pump_ms(0);

    if (WIFEXITED(stat)) {
        int exit_status = WEXITSTATUS(stat);
        if (exit_status == 99) {
            test->val_err = true;
            ++ptyh.val_errs;
            test_push_err("valgrind error(s)");
        }

        if (exit_status == 199) {
            Unity.CurrentTestFailed = true;
            linc_string_clear(&test->fail_msg);
            test_push_err("failed to exec test program");
            test->fail_exec = true;
        }

        linc_string_appendf(&test->exit_msg, "Exited with status %d",
                exit_status);
    } else if (WIFSIGNALED(stat)) {
        Unity.CurrentTestFailed = true;
        int term_sig = WTERMSIG(stat);
        linc_string_appendf(&test->exit_msg, "Terminated by signal %d (%s)",
                term_sig, strsignal(term_sig));
        test_push_err("Unexpected termination: %d (%s)",
                term_sig, strsignal(term_sig));
    } else {
        xtest_fatal("Unexpected wstat");
    }

    add_lines(&test->shlog_lines, test->shell_data.cstr);
    add_lines(&test->val_lines, test->val_data.cstr);

    for (size_t i = 0; i < test->shlog_lines.size; ++i) {
        char *line = test->shlog_lines.data[i].cstr;
        if (strncmp("ERROR", line, 5) == 0 ||
            strncmp("WARN", line, 4) == 0) {
            Unity.CurrentTestFailed = true;
            test_push_err("suspicious shell log");
        }
    }

    test->test_ok = (Unity.CurrentTestFailed == false);

    if (!test->test_ok)
        ++ptyh.fails;

    if (clock_gettime(CLOCK_MONOTONIC, &tend) == -1)
        xtest_fatal("clock_gettime");

    test->duration = (tend.tv_sec - tstart.tv_sec) +
        (tend.tv_nsec - tstart.tv_nsec) / 1e9;
}

void test_push_err(const char *fmt, ...) {
    char *msg;
    va_list va;

    va_start(va, fmt);
    if (vasprintf(&msg, fmt, va) == -1)
        xtest_fatal("vasprintf");
    va_end(va);

    if (test->fail_msg.len > 0)
        linc_string_append(&test->fail_msg, " && ");

    linc_string_append(&test->fail_msg, msg);
    free(msg);
}

void test_setup_imp(char **argv) {
    assert(argv);

    test = linc_array_push(&ptyh.tests);
    *test = (pty_test){0};

    struct winsize ws = { .ws_row = 15, .ws_col = 60 };

    test->argv = create_exec_argv(argv);
    test->cpid = forkpty(&test->mfd, NULL, NULL, &ws);
    if (test->cpid == -1)
        xtest_fatal("forkpty");

    if (test->cpid == 0) {
        execvp(test->argv[0], test->argv);
        fprintf(stderr, "test_setup: execvp: %m\n");
        fflush(stderr);
        _exit(199);
    }

    if (ioctl(test->mfd, FIONBIO, &(int){1}) == -1)
        xtest_fatal("ioctl");

    test->vt = vt_new_vterm(&test->mfd, ws.ws_row, ws.ws_col);
    test->test_name = strdup(Unity.CurrentTestName);
    test->test_file = strdup(Unity.TestFile);

    test->pty_out_data = LINC_STRING_INIT;
    test->pty_in_data = LINC_STRING_INIT;
    test->shell_data = LINC_STRING_INIT;
    test->val_data = LINC_STRING_INIT;

    test->fail_msg = LINC_STRING_INIT;
    test->exit_msg = LINC_STRING_INIT;
    test->diff_msg = LINC_STRING_INIT;

    if (clock_gettime(CLOCK_MONOTONIC, &tstart) == -1)
        xtest_fatal("clock_gettime");
}

void test_teardown(void) {
    test_shutdown_prgm();

    test_write(
        "===%s===\n"
        "Test took %lf seconds\n"
        "Seashell %s",
        test->test_name,
        test->duration,
        test->exit_msg.cstr
    );

    linc_string arg_line = LINC_STRING_INIT;

    for (char **arg = test->argv; *arg != nullptr; ++arg)
        linc_string_appendf(&arg_line, "%s ", *arg);

    test_write("%s", arg_line.cstr);

    if (!test->test_ok || test->val_err) {
        print_test_fail();

        if (!test->fail_exec && test->diff_snaps) {
            get_diff_msg();
            test_write("===DIFF===");
            test_write("%s", test->diff_msg.cstr);
        }

        log_test();
    } else if (verbose) {
        print_test_pass();
    }

    vterm_free(test->vt.vp);
    linc_string_free(&arg_line);
    close(test->mfd);
}
