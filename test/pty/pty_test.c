#include <assert.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <ncurses.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "linc_tools.h"

#include "pty_test.h"
#include "proc_view.h"
#include "unity.h"

da_pty_test all_tests;
bool verbose_mode = false;
bool run_valgrind = false;

int sh_pipe[2];
int val_pipe[2];
char *shell_lfd_arg;
char *val_lfd_arg;
dstr unity_buf;
char pty_expect_buf[BUFSIZ];

struct termios tio;
struct pollfd pollfds[NFDS];

bool first = true;

struct timespec start, end;
pty_test *test;

// LLOG SET UP
void pty_test_sink(const llog_info *info) {
    char *lvl_str;
    switch (info->log_level) {
    case LLOG_INFO: lvl_str = "INFO"; break;
    case LLOG_WARN: lvl_str = "WARN"; break;
    case LLOG_ERR:  lvl_str = "ERROR"; break;
    }

    char log_buf[BUFSIZ];

    int n = snprintf(log_buf, BUFSIZ, "%s: %s\n", lvl_str, info->msg);

    if (n < 0)
        return;

    if (n >= BUFSIZ) {
        log_buf[BUFSIZ - 2] = '\n';
        log_buf[BUFSIZ - 1] = '\0';
    }

    add_lines(&test->test_lines, log_buf);
}
static llog pty_log = { .sink = pty_test_sink };
LLOG_DEFAULT(&pty_log);

// XFUNCS SET UP
XFATAL_HANDLER(pty_xfatal_func) {
    endwin();
    fprintf(stderr, "%s:%d: ERROR %s: %s\n", XFUNC, XLINE, XSYSNAME,
            strerror(XERRNO));
    _exit(EXIT_FAILURE);
}
static xfatal xf = { .handler = pty_xfatal_func };
XFATAL_DEFAULT(&xf);

void harness_init(int argc, const char *const *argv) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp("-v", argv[i]) == 0) {
            verbose_mode = true;
        } else if (strcmp("-m", argv[i]) == 0)
            run_valgrind = true;
    }

    da_init(&all_tests);
    xtcgetattr(STDERR_FILENO, &tio);

    for (int i = 0; i < NFDS; ++i)
        pollfds[i].events = POLLIN;

    xpipe2(sh_pipe, O_NONBLOCK);
    pollfds[POLL_SHELL].fd = sh_pipe[0];
    if (asprintf(&shell_lfd_arg, "--logfd=%d", sh_pipe[1]) < 0)
        TEST_FAIL_MESSAGE("asprintf: input output error");

    xpipe2(val_pipe, O_NONBLOCK);
    pollfds[POLL_VAL].fd = val_pipe[0];
    if (asprintf(&val_lfd_arg, "--log-fd=%d", val_pipe[1]) < 0)
        TEST_FAIL_MESSAGE("asprintf: input output error");
}

void curs_err(const char *msg) {
    endwin();
    fprintf(stderr, "ERROR ncurses: %s\n", msg);
    _exit(EXIT_FAILURE);
}

char *pty_output_process(const char *msg) {
    dstr out;
    dstr_init(&out);

    for (const char *c = msg; *c != '\0'; ++c) {
        if (*c == '\n') {
            dstrcat(&out, "\r\n");
        } else
            dstr_push(&out, *c);
    }

    return out.c_str;
}

void print_valgrind_overview(void) {
    printf("\nValgrind memcheck -- ");

    if (val_test_fail_count() == 0) {
        printf("\x1b[97;42mCLEAN\x1b[m\n");
    } else {
        printf("\x1b[97;41m%d TESTS DIRTY\x1b[m\n", val_test_fail_count());
    }
}

dstr *push_line(da_dstr *lines) {
    dstr *line = da_push(lines);
    dstr_init(line);
    return line;
}

dstr *last_line(da_dstr *lines) {
    return &lines->data[lines->size - 1];
}

void add_lines(da_dstr *lines, const char *str) {
    assert(lines && str);

    if (strcmp(str, "") == 0)
        return;

    if (lines->size == 0)
        push_line(lines);

    for (const char *c = str; *c != '\0'; ++c) {
        if (*c == '\n') {
            push_line(lines);
            continue;
        }

        dstr_push(last_line(lines), *c);
    }
}

void sleep_ms(int ms) {
    int sec = ms / 1000;
    ms %= 1000;

    int ns = ms * 1000000;

    struct timespec ts = {
        .tv_sec = sec, .tv_nsec = ns,
    };

    if (nanosleep(&ts, NULL) == -1) {
        LOG_ERR("nanosleep: %m");
        exit(EXIT_FAILURE);
    }
}

static struct itimerspec ms_to_its(int ms) {
    int sec = ms / 1000;
    ms %= 1000;

    int ns = ms * 1000000;

    struct itimerspec its = {
        .it_value = {.tv_sec = sec, .tv_nsec = ns},
        .it_interval = {0}
    };

    return its;
}

void set_timerfd(int tfd, int ms) {
    struct itimerspec its = ms_to_its(ms);
    xtimerfd_settime(tfd, 0, &its, NULL);
}

int start_timerfd(int ms) {
    struct itimerspec its;
    its = ms_to_its(ms);

    int tfd = xtimerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    xtimerfd_settime(tfd, 0, &its, NULL);

    return tfd;
}

#define flush_pty(_fd) \
    flush_stream(_fd, true)
#define flush_pipe(_fd) \
    flush_stream(_fd, false)

char *flush_stream(int fd, bool break_on_eio) {
#define FLUSH_STREAM_BS 4096
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1) {
        endwin();
        perror("fcntl");
        _exit(EXIT_FAILURE);
    }

    if (!(flags & O_NONBLOCK)) {
        endwin();
        fprintf(stderr, "flush_stream receieved blocking fd\n");
        _exit(EXIT_FAILURE);
    }

    char *out = NULL;
    size_t out_size = 0;

    while (true) {
        char pbuf[FLUSH_STREAM_BS];
        int num_read = read(fd, pbuf, FLUSH_STREAM_BS);

        if (num_read == -1) {
            if (errno == EAGAIN) {
                break;
            } else if (break_on_eio && errno == EIO) {
                break;
            } else {
                endwin();
                perror("read");
                _exit(EXIT_FAILURE);
            }
        }

        out = xrealloc(out, out_size + num_read);
        memcpy(out + out_size, pbuf, num_read);
        out_size += num_read;
    }

    out = xrealloc(out, out_size + 1);
    out[out_size] = '\0';

    return out;
}

void free_vt_snap(vt_snap *snap) {
    assert(snap);
    for (size_t i = 0; i < snap->lines.size; ++i)
        free(snap->lines.data[i]);
    da_free(&snap->lines);
    *snap = (vt_snap){0};
}

void get_vt_snap(vt_snap *snap) {
    assert(snap);
    *snap = (vt_snap){0};

    vterm_state_get_cursorpos(test->vterm.st, &snap->curs_pos);

    for (int row = 0; row < test->vterm.rows; ++row) {
        char **line = da_push(&snap->lines);
        *line = xcalloc(1, test->vterm.cols + 1);

        for (int col = 0; col < test->vterm.cols; ++col) {
            VTermScreenCell cell;
            VTermPos pos = { .row = row, .col = col };

            if (!vterm_screen_get_cell(test->vterm.scr, pos, &cell))
                TEST_FAIL_MESSAGE("vterm_screen_get_cell");

            uint32_t cp = cell.chars[0];
            if (cp == 0)
                cp = ' ';

            if (cp > 127)
                TEST_FAIL_MESSAGE("non ascii in vterm");

            char ch = cp;

            if (ch == 0)
                ch = ' ';

            (*line)[col] = ch;
        }
    }
}

void take_vt_snapshot(void) {
    vt_snap *snap = da_push(&test->snaps);
    get_vt_snap(snap);
    snap->rows = test->vterm.rows;
    snap->cols = test->vterm.cols;
}

void test_resize_term(int row, int col) {
    if (row < 0 || col < 0)
        TEST_FAIL_MESSAGE("test_resize_term: bad row/col");

    vterm_set_size(test->vterm.tty, row, col);
    test->vterm.rows = row;
    test->vterm.cols = col;

    struct winsize ws = { .ws_row = row, .ws_col = col };
    if (ioctl(test->mfd, TIOCSWINSZ, &ws) == -1)
        TEST_FAIL_MESSAGE("ioctl");
}

bool verify_cursor(int crow, int ccol) {
    if (crow < 0 || crow >= test->vterm.rows)
        TEST_FAIL_MESSAGE("verify_cursor: out of bounds start row");
    if (ccol < 0 || ccol >= test->vterm.cols)
        TEST_FAIL_MESSAGE("verify_cursor: out of bounds start col");

    VTermPos curs_pos;
    vterm_state_get_cursorpos(test->vterm.st, &curs_pos);

    bool rv = (crow == curs_pos.row && ccol == curs_pos.col);
    return rv;
}

bool verify_vterm_state(const char *exp, vt_snap *snap) {
    assert(exp && snap);
    assert((int)strlen(exp) == test->vterm.rows * test->vterm.cols);

    get_vt_snap(snap);

    for (int row = 0; row < test->vterm.rows; ++row) {
        char *line = snap->lines.data[row];

        for (int col = 0; col < test->vterm.cols; ++col) {
            if (exp[(row * test->vterm.cols) + col] != line[col])
                return false;
        }
    }

    return true;
}

void write_to_term(const char *str) {
    const char *c = str;
    size_t left = strlen(str);
    while (left != 0) {
        size_t n = vterm_input_write(test->vterm.tty, c, left);
        c += n;
        left -= n;
    }
}

int pty_test_poll(int tfd, bool log_mfd) {
    pollfds[POLL_TFD] = (struct pollfd){ .fd = tfd, .events = POLLIN };

    int rv = -1;
    char *data = NULL;

    while (true) {
        xpoll(pollfds, NFDS, -1);

        if (pollfds[POLL_TFD].revents & POLLIN) {
            rv = PTY_POLL_TFD;
            goto cleanup;
        }

        if (pollfds[POLL_SHELL].revents & POLLIN) {
            data = flush_pipe(sh_pipe[0]);
            add_lines(&test->shell_log_lines, data);
            rv = PTY_POLL_SHELL;
            goto cleanup;
        }

        if (pollfds[POLL_VAL].revents & POLLIN) {
            data = flush_pipe(val_pipe[0]);
            add_lines(&test->val_lines, data);
            rv = PTY_POLL_VAL;
            goto cleanup;
        }

        if (pollfds[POLL_MFD].revents & POLLIN) {
            data = flush_pty(test->mfd);
            if (log_mfd)
                LOG_INFO("mfd: %s", data);

            dstrcat(&test->ptydata, data);
            add_lines(&test->raw_lines, data);
            write_to_term(data);
            rv = PTY_POLL_MFD;
            goto cleanup;
        }
    }

cleanup:
    free(data);
    return rv;
}

bool kill_child(const char *name) {
    da_pstat pstats;
    size_t loops = 100;
    int tfd = start_timerfd(1);

    for (size_t i = 0; i < loops; ++i) {
        if (child_pstat(test->cpid, &pstats) == -1)
            TEST_FAIL_MESSAGE("child_pstat");

        for (size_t y = 0; y < pstats.size; ++y) {
            if (strcmp(pstats.data[y].name, name) == 0) {
                if (test->cpid == pstats.data[y].pid) {
                    LOG_ERR("trying to kill seashell");
                    da_free(&pstats);
                    xclose(tfd);
                    return false;
                }

                xkill(pstats.data[y].pid, SIGTERM);
                da_free(&pstats);
                xclose(tfd);
                return true;
            }
        }

        while (pty_test_poll(tfd, false) != PTY_POLL_TFD)
            continue;

        set_timerfd(tfd, 1);
    }

    xclose(tfd);
    da_free(&pstats);
    return false;
}

bool verify_pstate(const char *name, const char *state) {
    size_t loops = 100;
    da_pstat pstats;
    bool state_good = false;
    char pstate[4096];
    int tfd = start_timerfd(1);

    for (size_t i = 0; i < loops; ++i) {
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

        while (pty_test_poll(tfd, false) != PTY_POLL_TFD)
            continue;

        set_timerfd(tfd, 1);
    }

    if (!state_good)
        LOG_ERR("Bad state: %s", state);

    xclose(tfd);
    return state_good;
}

bool verify_termios(struct termios exp) {
    struct termios tio;

    int sleep_time = (run_valgrind) ? 1000 : 100;
    int tfd = start_timerfd(1);

    for (int i = 0; i < sleep_time; ++i) {
        xtcgetattr(test->mfd, &tio);
        if (memcmp(exp.c_cc, tio.c_cc, sizeof(tio.c_cc))            == 0 &&
            memcmp(&exp.c_iflag, &tio.c_iflag, sizeof(tio.c_iflag)) == 0 &&
            memcmp(&exp.c_oflag, &tio.c_oflag, sizeof(tio.c_oflag)) == 0 &&
            memcmp(&exp.c_cflag, &tio.c_cflag, sizeof(tio.c_cflag)) == 0 &&
            memcmp(&exp.c_lflag, &tio.c_lflag, sizeof(tio.c_lflag)) == 0)
        {
            xclose(tfd);
            return true;
        }

        while (pty_test_poll(tfd, false) != PTY_POLL_TFD)
            continue;

        set_timerfd(tfd, 1);
    }

    xclose(tfd);
    return false;
}

char *lines_to_term(char **lines) {
#define VTERM_ROWS_MAX 4096
    assert(lines);
    assert(test->vterm.rows <= 4096);

    for (int i = 0; i < test->vterm.rows && lines[i]; ++i) {
        if (i == test->vterm.rows - 1 && lines[i + 1]) {
            LOG_ERR("Too many terminal lines in assertion");
            TEST_FAIL_MESSAGE("Too many terminal lines in assertion");
        }
    }

    char *all_line = nullptr;
    bool lines_done = false;
    char spacing[VTERM_ROWS_MAX];
    memset(spacing, ' ', VTERM_ROWS_MAX);

    for (int i = 0; i < test->vterm.rows; ++i) {
        if (!lines_done && lines[i] == NULL)
            lines_done = true;

        int gap = test->vterm.cols;

        if (!lines_done) {
            if (test->vterm.cols < (int)strlen(lines[i])) {
                LOG_ERR("bad line request, len=%d, cols=%d: [%s]",
                    (int)hds_len(lines[i]), test->vterm.cols, lines[i]);
                TEST_FAIL_MESSAGE("bad line request");
            }

            hds_append(all_line, lines[i]);
            gap -= (int)strlen(lines[i]);
        }

        hds_append_n(all_line, spacing, gap);
    }

    assert((int)strlen(all_line) == test->vterm.rows * test->vterm.cols);
    return all_line;
}

bool term_ensure(char **lines, int crow, int ccol) {
    assert(crow >= 0 && ccol >= 0);
    assert(crow < test->vterm.rows && ccol < test->vterm.cols);

    vt_snap snap;
    VTermPos curs_pos;
    bool result = false;
    char *exp = lines_to_term(lines);

    if (verify_vterm_state(exp, &snap) && verify_cursor(crow, ccol)) {
        free_vt_snap(&snap);
        hds_free(exp);
        return true;
    }

    int tfd = start_timerfd((run_valgrind) ? 1000 : 100);

    while (true) {
        int poll_rv = pty_test_poll(tfd, false);

        if (poll_rv == PTY_POLL_TFD) {
            LOG_ERR("--LINES--");
            for (int i = 0; i < test->vterm.rows; ++i)
                LOG_ERR("[%.*s] [%.*s]", test->vterm.cols,
                        exp + (i * test->vterm.cols), test->vterm.cols,
                        snap.lines.data[i]);
            LOG_ERR("");
            vterm_state_get_cursorpos(test->vterm.st, &curs_pos);
            LOG_ERR("--CURSOR--");
            LOG_ERR("[%d,%d] [%d,%d]",
                crow, ccol, curs_pos.row, curs_pos.col);

            goto cleanup;

        } else if (poll_rv == PTY_POLL_MFD) {
            free_vt_snap(&snap);

            if (verify_vterm_state(exp, &snap) && verify_cursor(crow, ccol)) {
                result = true;
                goto cleanup;
            }
        }
    }

cleanup:
    xclose(tfd);
    free_vt_snap(&snap);
    hds_free(exp);
    return result;
}

void child_pstat_snapshot(void) {
#define INFO_BUF_SIZE 4096
    char info_buf[INFO_BUF_SIZE];

    da_pstat pstats;
    if (child_pstat(test->cpid, &pstats) == -1)
        TEST_FAIL_MESSAGE("child_pstat");

    if (pstats.size > 0) {
        snprintf(info_buf, INFO_BUF_SIZE, "seashell (%d)\n", test->cpid);
        add_lines(&test->test_lines, info_buf);

        for (size_t i = 0; i < pstats.size; ++i) {
            add_lines(&test->test_lines, "    ");
            snprintf(info_buf, INFO_BUF_SIZE, "%s (%d)\n",
                    pstats.data[i].name, pstats.data[i].pid);
            add_lines(&test->test_lines, info_buf);
        }
    } else {
        snprintf(info_buf, INFO_BUF_SIZE, "seashell (%d) no children\n",
                test->cpid);
        add_lines(&test->test_lines, info_buf);
    }

    da_free(&pstats);
}

static void vterm_output_cb(const char *bytes, size_t len, void *user) {
    int mfd = *(int *)user;
    if ((size_t) write(mfd, bytes, len) != len)
        TEST_FAIL_MESSAGE("vterm_output_cb: write: failed/partial");
}

void vterm_send_char(char ch, VTermModifier mod) {
    VTerm *vtty = test->vterm.tty;
    vterm_keyboard_unichar(vtty, ch, mod);
}

void vterm_send_str(const char *msg) {
    VTerm *vtty = test->vterm.tty;
    for (const char *c = msg; *c != '\0'; ++c) {
        uint32_t key = *c;

        if (*c == '\n') {
            vterm_keyboard_key(vtty, VTERM_KEY_ENTER, VTERM_MOD_NONE);
        } else if (*c == '\b') {
            vterm_keyboard_key(vtty, VTERM_KEY_BACKSPACE, VTERM_MOD_NONE);
        } else {
            vterm_keyboard_unichar(vtty, key, VTERM_MOD_NONE);
        }
    }
}

void vterm_send_key(VTermKey key, VTermModifier mod) {
    vterm_keyboard_key(test->vterm.tty, key, mod);
}

int test_fail_count(void) {
    int fail_n = 0;

    for (size_t i = 0; i < all_tests.size; ++i)
        if (!all_tests.data[i].success || all_tests.data[i].val_err)
            ++fail_n;

    return fail_n;
}

int val_test_fail_count(void) {
    int vall_err_n = 0;

    for (size_t i = 0; i < all_tests.size; ++i)
        if (all_tests.data[i].val_err)
            ++vall_err_n;

    return vall_err_n;
}

void exec_seashell(void) {
    char **argv;

    if (run_valgrind) {
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

void new_shell(void) {
    if (clock_gettime(CLOCK_MONOTONIC, &start) == -1)
        TEST_FAIL_MESSAGE("clock_gettime");

    test = da_push(&all_tests);
    *test = (pty_test){0};

    /* init pty */
    struct winsize ws = { .ws_row = 15, .ws_col = 45 };

    test->cpid = xforkpty(&test->mfd, NULL, &tio, &ws);
    if (test->cpid == 0)
        exec_seashell();
    pollfds[POLL_MFD].fd = test->mfd;

    if (ioctl(test->mfd, FIONBIO, &(int){1}) == -1)
        TEST_FAIL_MESSAGE("ioctl");

    /* init vterm */
    test->vterm.tty = vterm_new(ws.ws_row, ws.ws_col);
    if (!test->vterm.tty)
        TEST_FAIL_MESSAGE("vterm_new");
    test->vterm.rows = ws.ws_row;
    test->vterm.cols = ws.ws_col;

    vterm_set_utf8(test->vterm.tty, 1);

    test->vterm.scr = vterm_obtain_screen(test->vterm.tty);
    if (!test->vterm.scr)
        TEST_FAIL_MESSAGE("vterm_obtain_screen");

    test->vterm.st = vterm_obtain_state(test->vterm.tty);
    if (!test->vterm.st)
        TEST_FAIL_MESSAGE("vterm_obtain_state");

    vterm_screen_reset(test->vterm.scr, 1);
    vterm_screen_enable_reflow(test->vterm.scr, true);

    vterm_output_set_callback(
        test->vterm.tty,
        vterm_output_cb,
        &test->mfd
    );

    /* init misc data */
    dstr_init(&test->ptydata);
    test->test_name = strdup(Unity.CurrentTestName);

    /* seashell initializes printty (termios) then displays
     * the prompt so this works to avoid writing to the mfd
     * before canonical mode is disabled thus having the write
     * echoed by the tty driver. */

    /* assert the cursor *before* the screen contents. */

    assert_term(0, 2, PROMPT, " ");
}

void delete_shell(void) {
    /* so the shell can write any final data to the mfd before
    *  exiting. If we don't sleep the transcript might be cut off.
    *  Also if the shell sends a dsr request allow the request to
    *  be passed to vterm so seashell doesn't fail becuase it did
    *  not receive the request. */

    xkill(test->cpid, SIGHUP);

    int checks = (run_valgrind) ? 1000 : 100;
    int tfd = start_timerfd(1);
    int rv, stat, i;

    for (i = 0; i < checks; ++i) {
        if ((rv = waitpid(test->cpid, &stat, WNOHANG)) == -1) {
            LOG_ERR("waitpid: %m");
            exit(EXIT_FAILURE);
        }

        if (rv != 0)
            break;

        while (pty_test_poll(tfd, false) != PTY_POLL_TFD)
            continue;

        set_timerfd(tfd, 1);
    }

    xclose(tfd);

    if (i == 100) {
        LOG_ERR("waitpid timed out");
        xkill(test->cpid, SIGKILL);

        if (waitpid(test->cpid, &stat, 0) == -1) {
            LOG_ERR("waitpid: %m");
            exit(EXIT_FAILURE);
        }
    }

    LOG_INFO("");
    LOG_INFO("TEST SUMMARY");
    LOG_INFO("============");

    if (WIFEXITED(stat)) {
        int exit_status = WEXITSTATUS(stat);

        if (exit_status == 99)
            test->val_err = true;

        LOG_INFO("seashell exited with status %d", exit_status);

        test->success = !Unity.CurrentTestFailed;
    } else {
        if (WIFSIGNALED(stat)) {
            LOG_INFO("seashell was terminated by signal %d (%s)",
                    WTERMSIG(stat), strsignal(WTERMSIG(stat)));
        }

        /* seashell should not be terminated by a signal. If
         * this is true then we probably sent SIGKILL to it
         * becuase it hung. Or segfaulted. */
        Unity.CurrentTestFailed = true;
        test->success = false;
    }

    if (!test->success) {
        take_vt_snapshot();
    }

    vterm_free(test->vterm.tty);
    close(test->mfd);

    if (clock_gettime(CLOCK_MONOTONIC, &end) == -1) {
        LOG_ERR("clock_gettime");
        exit(EXIT_FAILURE);
    }

    test->dur = (end.tv_sec - start.tv_sec) +
        (end.tv_nsec - start.tv_nsec) / 1e9;

    LOG_INFO("test took %lf seconds", test->dur);
    LOG_INFO("");
    LOG_INFO("pty data: %s", test->ptydata.c_str);
}
