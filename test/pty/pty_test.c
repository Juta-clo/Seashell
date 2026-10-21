#include <assert.h>
#include <signal.h>
#include <time.h>
#include <ncurses.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "linc_tools.h"
#include "pty_test.h"
#include "proc_view.h"
#include "unity_fixture.h" // IWYU pragma: keep
#include "tutils.h"

#define TEST_BIN_DIR "./test/pty/test_bins"

typedef enum {
    POLL_MFD,
    POLL_SHELL,
    POLL_VAL,
    POLL_TFD,
    NFDS,
} poll_fds;

bool verbose = false;
bool memcheck = false;
bool script = true;

pty_harness ptyh;
pty_test *test;

struct pollfd pollfds[NFDS];
struct timespec tstart, tend;
struct termios tio;

int sh_pipe[2];
int val_pipe[2];

FILE *log_file = nullptr;
FILE *log_raw_file = nullptr;
char *shell_lfd_arg;
char *val_lfd_arg;

static void test_open_log_files(void) {
    char log_path[] = "/tmp/shlog-XXXXXX";
    char log_raw_path[] = "/tmp/shlog-raw-XXXXXX";

    log_file = new_tmp_file(log_path);
    log_raw_file = new_tmp_file(log_raw_path);

    test_relink(log_path, "/tmp/shlog-latest");
    test_relink(log_raw_path, "/tmp/shlog-latest.raw");
}

static void test_write_to_term(const char *str) {
    const char *c = str;
    size_t left = strlen(str);
    while (left != 0) {
        size_t n = vterm_input_write(test->vt.vp, c, left);
        c += n;
        left -= n;
    }
}

static int test_pty_poll(int tfd) {
    pollfds[POLL_TFD] = (struct pollfd){ .fd = tfd, .events = POLLIN };

    char *data = NULL;

    while (true) {
        if (poll(pollfds, NFDS, -1) == -1)
            xtest_fatal("poll: %m");

        if (pollfds[POLL_TFD].revents & POLLIN)
            return PTY_POLL_TFD;

        if (pollfds[POLL_MFD].revents & POLLIN) {
            data = flush_pty(test->mfd);
            dstrcat(&test->ptydata, data);
            add_lines(&test->raw_lines, data);
            test_write_to_term(data);
            free(data);
            return PTY_POLL_MFD;
        }

        if (pollfds[POLL_SHELL].revents & POLLIN) {
            data = flush_pipe(sh_pipe[0]);
            add_lines(&test->shlog_lines, data);
            free(data);
        }

        if (pollfds[POLL_VAL].revents & POLLIN) {
            data = flush_pipe(val_pipe[0]);
            add_lines(&test->val_lines, data);
            free(data);
        }
    }
}

static void test_exec_pgrm(void) {
    char **argv;

    if (memcheck) {
        argv = (char *[]){
            "valgrind",
                "--vgdb=no",
                "--leak-check=full",
                "--errors-for-leak-kinds=definite,indirect",
                "--show-leak-kinds=definite,indirect",
                "--error-exitcode=99", /*seashell shouldn't return that*/
                "--track-fds=bad", /*this is awesome*/
                "--modify-fds=high",
                /* this is bugged with sigsuspend. See bugzilla#526386 */
                "--enable-debuginfod=no",
                val_lfd_arg,

            "/home/juta/Projects/Seashell/seashell",
                "--prompt=simple",
                shell_lfd_arg,
            NULL
        };
    } else {
        argv = (char *[]){
            "/home/juta/Projects/Seashell/seashell",
                "--prompt=simple",
                shell_lfd_arg,
            NULL
        };
    }

    execvp(argv[0], argv);
    _exit(EXIT_FAILURE);
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
    int tfd = start_timerfd(1);
    int rv, stat, i;

    for (i = 0; i < checks; ++i) {
        if ((rv = waitpid(test->cpid, &stat, WNOHANG)) == -1)
            xtest_fatal("waitpid: %m");

        if (rv != 0)
            break;

        while (test_pty_poll(tfd) != PTY_POLL_TFD)
            continue;

        set_timerfd(tfd, 1);
    }

    if (close(tfd) == -1)
        xtest_fatal("close: %m");

    if (i == checks) {
        dstrcat(&test->fail_msg, " && waitpid timed out");
        if (kill(test->cpid, SIGKILL) == -1)
            xtest_fatal("kill: %m");

        if (waitpid(test->cpid, &stat, 0) == -1)
            xtest_fatal("waitpid: %m");
    }

    if (WIFEXITED(stat)) {
        int exit_status = WEXITSTATUS(stat);

        if (exit_status == 99) {
            ++ptyh.val_errs;
            test->val_err = true;
        }

        test->test_ok = !Unity.CurrentTestFailed;
        dstr_printf(&test->exit_msg, "Exited with status %d", exit_status);
    } else {
        Unity.CurrentTestFailed = true;
        test->test_ok = false;

        if (!WIFSIGNALED(stat))
            xtest_fatal("Unexpected wstat");

        dstr_printf(&test->exit_msg, "Terminated by signal %d (%s)",
                WTERMSIG(stat), strsignal(WTERMSIG(stat)));
    }

    if (!test->test_ok)
        ++ptyh.fails;

    if (clock_gettime(CLOCK_MONOTONIC, &tend) == -1)
        xtest_fatal("clock_gettime");

    test->duration = (tend.tv_sec - tstart.tv_sec) +
        (tend.tv_nsec - tstart.tv_nsec) / 1e9;
}

static void get_diff_msg(void) {
    if (test->vt.rows != test->vt_act.rows ||
        test->vt.rows != test->vt_exp.rows)
        xtest_fatal("unexpected row count");

    bool curs_dirty;
    da_int dirty_lines = vt_ch_dirty(&test->vt_exp, &test->vt_act, &curs_dirty);

    if (curs_dirty) {
        dstr_printf(&test->diff_msg, "cursor DIRTY\n");
        dstr_printf(&test->diff_msg, "exp[%d,%d] act[%d,%d]\n",
                test->vt_exp.crow, test->vt_exp.ccol,
                test->vt_act.crow, test->vt_act.ccol);
    } else {
        dstr_printf(&test->diff_msg, "cursor OK\n");
    }

    if (dirty_lines.size != 0) {
        dstr_printf(&test->diff_msg, "vterm state DIRTY\n");
        dstr_printf(&test->diff_msg, "---expected\n");
        dstr_printf(&test->diff_msg, "+++actual\n");

        for (size_t i = 0; i < dirty_lines.size; ++i) {
            int ind = dirty_lines.data[i];
            char *exp = test->vt_exp.lines.data[ind];
            char *act = test->vt_act.lines.data[ind];

            dstr_printf(&test->diff_msg, "%d -%s\n", ind, exp);
            dstr_printf(&test->diff_msg, "%d +%s\n", ind, act);
        }
    } else {
        dstr_printf(&test->diff_msg, "vterm state OK\n");
    }
}

void test_harness_init(int argc, const char *const *argv) {
    ptyh = (pty_harness){0};

    for (int i = 1; i < argc; ++i) {
        if (strcmp("-v", argv[i]) == 0) {
            verbose = true;
        } else if (strcmp("-m", argv[i]) == 0) {
            memcheck = true;
        } else if (strcmp("-s", argv[i]) == 0)
            script = false;
    }

    da_init(&ptyh.tests);
    if (tcgetattr(STDERR_FILENO, &tio) == -1)
        xtest_fatal("tcgetattr: %m");

    for (int i = 0; i < NFDS; ++i)
        pollfds[i].events = POLLIN;

    if (pipe2(sh_pipe, O_NONBLOCK) == -1)
        xtest_fatal("tcgetattr: %m");
    pollfds[POLL_SHELL].fd = sh_pipe[0];
    if (asprintf(&shell_lfd_arg, "--logfd=%d", sh_pipe[1]) < 0)
        xtest_fatal("asprintf: %m");

    if (pipe2(val_pipe, O_NONBLOCK) == -1)
        xtest_fatal("pipe2: %m");
    pollfds[POLL_VAL].fd = val_pipe[0];
    if (asprintf(&val_lfd_arg, "--log-fd=%d", val_pipe[1]) < 0)
        xtest_fatal("asprintf: %m");

    test_open_log_files();

    if (mkdir("/tmp/seashell", 0700) == -1 && errno != EEXIST)
        xtest_fatal("mkdir: %m");

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
}

void test_harness_deinit(void) {
    if (log_file)
        fclose(log_file);
    if (log_raw_file)
        fclose(log_raw_file);
    system("find /tmp/seashell -maxdepth 1 -type f -delete");
    rmdir("/tmp/seashell");
}

void test_setup(void) {
    test = da_push(&ptyh.tests);
    *test = (pty_test){0};

    struct winsize ws = { .ws_row = 15, .ws_col = 60 };

    test->cpid = forkpty(&test->mfd, NULL, &tio, &ws);
    if (test->cpid == -1)
        xtest_fatal("forkpty");

    if (test->cpid == 0)
        test_exec_pgrm();

    pollfds[POLL_MFD].fd = test->mfd;

    if (ioctl(test->mfd, FIONBIO, &(int){1}) == -1)
        xtest_fatal("ioctl");

    test->vt = vt_new_vterm(&test->mfd, ws.ws_row, ws.ws_col);
    test->test_name = strdup(Unity.CurrentTestName);
    test->test_file = strdup(Unity.TestFile);

    dstr_init(&test->ptydata);
    dstr_init(&test->fail_msg);
    dstr_init(&test->exit_msg);
    dstr_init(&test->diff_msg);

    if (clock_gettime(CLOCK_MONOTONIC, &tstart) == -1)
        xtest_fatal("clock_gettime");

    /* seashell *must* init termios before we write! */
    assert_term(0, 2, PROMPT);
}

void test_teardown(void) {
    test_shutdown_prgm();

    test_write(
        "===%s===\n\n"
        "Test took %lf seconds\n"
        "Seashell %s\n",
        test->test_name,
        test->duration,
        test->exit_msg.c_str
    );

    if (!test->test_ok || test->val_err) {
        *da_push(&test->snaps) = vt_get_snap();
        get_diff_msg();

        fprintf(log_file, "%s:FAIL:%s:%d:%s\n",
                test->test_name, test->test_file, test->fail_line,
                test->fail_msg.c_str);

        test_write("===DIFF===");
        test_write("%s", test->diff_msg.c_str);

        fprintf(log_file, "===DIFF(` marks empty cell)===\n");
        fprintf(log_file, "%s", test->diff_msg.c_str);

        fprintf(log_file, "===SHELL LOGS===\n");
        for (size_t i = 0; i < test->shlog_lines.size; ++i) {
            dstr *line = &test->shlog_lines.data[i];
            fprintf(log_file, "%s", line->c_str);
            if (i != test->shlog_lines.size - 1)
                fprintf(log_file, "\n");
        }

        fprintf(log_file, "===VALGRIND OUTPUT===\n");

        if (test->val_lines.size == 0)
            fprintf(log_file, "[no data]\n");

        for (size_t i = 0; i < test->val_lines.size; ++i) {
            dstr *line = &test->val_lines.data[i];
            fprintf(log_file, "%s\n", line->c_str);
        }

        convert_special_chars(&test->ptydata.c_str);
        fprintf(log_raw_file, "%s\n", test->ptydata.c_str);
    }

    vterm_free(test->vt.vp);
    close(test->mfd);
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

bool verify_bytes(const char *str) {
    int tfd = start_timerfd((memcheck) ? 1000 : 100);
    size_t start = test->ptydata.len;

    while (test_pty_poll(tfd) != PTY_POLL_TFD) {
        if (start >= test->ptydata.len)
            continue;

        for (size_t i = start; i < test->ptydata.len; ++i) {
            if (strstr(&test->ptydata.c_str[i], str)) {
                if (close(tfd) == -1)
                    xtest_fatal("close: %m");
                return true;
            }
        }
    }

    if (close(tfd) == -1)
        xtest_fatal("close: %m");
    return false;
}

bool verify_kill(const char *name) {
    da_pstat pstats;
    size_t loops = 100;
    int tfd = start_timerfd(1);

    for (size_t i = 0; i < loops; ++i) {
        if (child_pstat(test->cpid, &pstats) == -1)
            xtest_fatal("child_pstat");

        for (size_t y = 0; y < pstats.size; ++y) {
            if (strcmp(pstats.data[y].name, name) == 0) {
                if (test->cpid == pstats.data[y].pid)
                    xtest_fatal("kill_child: trying to kill seashell");

                if (kill(pstats.data[y].pid, SIGTERM) == -1)
                    xtest_fatal("kill: %m");

                da_free(&pstats);

                if (close(tfd) == -1)
                    xtest_fatal("close: %m");

                return true;
            }
        }

        while (test_pty_poll(tfd) != PTY_POLL_TFD)
            continue;

        set_timerfd(tfd, 1);
    }

    if (close(tfd) == -1)
        xtest_fatal("close: %m");
    da_free(&pstats);
    return false;
}

bool verify_pstate(const char *name, const char *state) {
    da_pstat pstats;
    bool state_good = false;
    char pstate[4096];

    int tfd = start_timerfd(1);
    size_t checks = (memcheck) ? 1000 : 100;

    for (size_t i = 0; i < checks; ++i) {
        if (child_pstat(test->cpid, &pstats) == -1)
            break;

        for (size_t y = 0; y < pstats.size; ++y) {
            if (strcmp(name, pstats.data[y].name) == 0) {
                strncpy(pstate, pstats.data[y].state, sizeof(pstate));

                if (strncmp(state, pstate, 1) == 0) {
                    state_good = true;
                    break;
                }
            }
        }

        da_free(&pstats);

        if (state_good)
            break;

        while (test_pty_poll(tfd) != PTY_POLL_TFD)
            continue;

        set_timerfd(tfd, 1);
    }

    if (close(tfd) == -1)
        xtest_fatal("close: %m");

    return state_good;
}

bool verify_termios(struct termios exp) {
    struct termios tio;

    int checks = (memcheck) ? 1000 : 100;
    int tfd = start_timerfd(1);

    for (int i = 0; i < checks; ++i) {
        if (tcgetattr(test->mfd, &tio) == -1)
            xtest_fatal("tcgetattr: %m");
        if (memcmp(exp.c_cc, tio.c_cc, sizeof(tio.c_cc))            == 0 &&
            memcmp(&exp.c_iflag, &tio.c_iflag, sizeof(tio.c_iflag)) == 0 &&
            memcmp(&exp.c_oflag, &tio.c_oflag, sizeof(tio.c_oflag)) == 0 &&
            memcmp(&exp.c_cflag, &tio.c_cflag, sizeof(tio.c_cflag)) == 0 &&
            memcmp(&exp.c_lflag, &tio.c_lflag, sizeof(tio.c_lflag)) == 0)
        {
            if (close(tfd) == -1)
                xtest_fatal("close: %m");
            return true;
        }

        while (test_pty_poll(tfd) != PTY_POLL_TFD)
            continue;

        set_timerfd(tfd, 1);
    }

    if (close(tfd) == -1)
        xtest_fatal("close: %m");
    return false;
}

bool verify_term(char **lines, int crow, int ccol) {
    assert(crow >= 0 && ccol >= 0);
    assert(crow < test->vt.rows && ccol < test->vt.cols);

    vt_lines_to_snap(lines, &test->vt_exp, crow, ccol);

    if (vt_snap_cmp(&test->vt_exp)) {
        vt_free_snap(&test->vt_exp);
        return true;
    }

    int tfd = start_timerfd((memcheck) ? 1000 : 100);

    while (true) {
        int poll_rv = test_pty_poll(tfd);

        if (poll_rv == PTY_POLL_TFD) {
            test->vt_act = vt_get_snap();
            if (close(tfd) == -1)
                xtest_fatal("close: %m");

            return false;

        } else if (poll_rv == PTY_POLL_MFD) {
            if (vt_snap_cmp(&test->vt_exp)) {
                if (close(tfd) == -1)
                    xtest_fatal("close: %m");

                vt_free_snap(&test->vt_exp);
                return true;
            }
        }
    }
}
