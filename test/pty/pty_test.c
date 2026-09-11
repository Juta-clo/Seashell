#include <assert.h>
#include <errno.h>
#include <ncurses.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "pty_test.h"
#include "dstr.h"
#include "llog.h"
#include "proc_view.h"
#include "unity.h"
#include "xfuncs.h"

da_pty_test all_tests;
bool verbose_mode = false;
bool run_valgrind = false;

int sh_pipe[2];
int val_pipe[2];
char *shell_lfd_arg;
char *val_lfd_arg;
dstr unity_buf;

struct termios tio;
struct pollfd pollfds[NFDS];

bool first = true;

pty_test *cur_test;
dstr reqdata;

void harness_init(int argc, const char *const *argv) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp("-v", argv[i]) == 0) {
            verbose_mode = true;
        } else if (strcmp("-m", argv[i]) == 0)
            run_valgrind = true;
    }

    da_init(&all_tests);

    set_xfatal_handler(pty_xfatal_func);
    llog_set_sink(pty_test_sink);

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

XFATAL_HANDLER(pty_xfatal_func) {
    endwin();
    fprintf(stderr, "%s:%d: ERROR %s: %s\n", XFUNC, XLINE, XSYSNAME,
            strerror(XERRNO));
    _exit(EXIT_FAILURE);
}

void curs_err(const char *msg) {
    endwin();
    fprintf(stderr, "ERROR ncurses: %s\n", msg);
    _exit(EXIT_FAILURE);
}

LLOG_SINK(pty_test_sink) {
    char *lvl_str;
    switch (info->log_level) {
    case LLOG_INFO: lvl_str = "INFO"; break;
    case LLOG_WARN: lvl_str = "WARN"; break;
    case LLOG_ERR:  lvl_str = "ERROR"; break;
    }

    char log_buf[BUFSIZ];

    int n = snprintf(log_buf, BUFSIZ, "%s llog: %s:%d: %s\n",
            lvl_str, info->site->file, info->site->line, info->msg);

    if (n < 0)
        return;

    if (n >= BUFSIZ) {
        log_buf[BUFSIZ - 2] = '\n';
        log_buf[BUFSIZ - 1] = '\0';
    }

    add_lines(&cur_test->test_lines, log_buf);
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

int start_timerfd(int ms) {
    int sec = ms / 1000;
    ms %= 1000;

    int ns = ms * 1000000;

    struct itimerspec its = {
        .it_value = {.tv_sec = sec, .tv_nsec = ns},
        .it_interval = {0}
    };

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

void term_ensure(const char *msg, int srow, int scol) {
    size_t len = strlen(msg);

    if (!msg || len == 0)
        TEST_FAIL_MESSAGE("term_ensure: bad input");
    if (srow < 0 || srow >= cur_test->vterm.rows)
        TEST_FAIL_MESSAGE("term_ensure: out of bounds start row");
    if (scol < 0 || scol >= cur_test->vterm.cols)
        TEST_FAIL_MESSAGE("term_ensure: out of bounds start col");

    size_t n = 0;

    for (int row = srow; row < cur_test->vterm.rows; ++row) {
        scol = (row == srow) ? scol : 0;

        for (int col = scol; col < cur_test->vterm.cols; ++col) {
            VTermScreenCell cell;
            VTermPos pos = { .row = row, .col = col };

            if (!vterm_screen_get_cell(cur_test->vterm.scr, pos, &cell))
                TEST_FAIL_MESSAGE("vterm_screen_get_cell");

            uint32_t ch = cell.chars[0];
            if (ch == 0)
                ch = ' ';

            LOG_INFO("vterm.screen[%d, %d] = '%c'", row, col, ch);
            TEST_ASSERT_EQUAL_CHAR(ch, msg[n]);

            if (++n >= len)
                goto done;
        }
    }

done:
    if (n < len)
        TEST_FAIL_MESSAGE("could not find string in vterm");

    return;
}

void pty_ensure(const char *msg) {
    char *expected = pty_output_process(msg);
    size_t len = strlen(expected);

    char *pty_chars = request_chars(len, &cur_test->pred_i, 1000);

    TEST_ASSERT_EQUAL_size_t(len, strlen(pty_chars));
    TEST_ASSERT_EQUAL_STRING(expected, pty_chars);

    free(expected);
}

void child_pstat_snapshot(void) {
#define INFO_BUF_SIZE 4096
    char info_buf[INFO_BUF_SIZE];

    da_pstat pstats;
    if (child_pstat(cur_test->cpid, &pstats) == -1)
        TEST_FAIL_MESSAGE("child_pstat");

    if (pstats.size > 0) {
        snprintf(info_buf, INFO_BUF_SIZE, "seashell (%d)\n", cur_test->cpid);
        add_lines(&cur_test->test_lines, info_buf);

        for (size_t i = 0; i < pstats.size; ++i) {
            add_lines(&cur_test->test_lines, "    ");
            snprintf(info_buf, INFO_BUF_SIZE, "%s (%d)\n",
                    pstats.data[i].name, pstats.data[i].pid);
            add_lines(&cur_test->test_lines, info_buf);
        }
    } else {
        snprintf(info_buf, INFO_BUF_SIZE, "seashell (%d) no children\n",
                cur_test->cpid);
        add_lines(&cur_test->test_lines, info_buf);
    }

    da_free(&pstats);
}

void write_to_term(const char *str) {
    const char *c = str;
    size_t left = strlen(str);
    while (left != 0) {
        size_t n = vterm_input_write(cur_test->vterm.tty, c, left);
        c += n;
        left -= n;
    }
}

char *request_chars(size_t nchars, size_t *index, int ms) {
    assert(index);
    assert(cur_test->ptydata.len >= *index);

    if (nchars == 0) {
        LOG_WARN("requested 0 characters");
        return "";
    }

    int tfd = start_timerfd(ms);
    pollfds[POLL_TFD] = (struct pollfd){.fd = tfd, .events = POLLIN};

    while (true) {
        if (cur_test->ptydata.len - *index >= nchars) {
            dstrncpy(&reqdata, &cur_test->ptydata.c_str[*index], nchars);
            *index += nchars;
            goto done;
        }

        xpoll(pollfds, NFDS, -1);

        if (pollfds[POLL_TFD].revents & POLLIN) {
            TEST_FAIL_MESSAGE("timed out waiting for pty data");
        }

        if (pollfds[POLL_SHELL].revents & POLLIN) {
            char *data = flush_pipe(sh_pipe[0]);
            add_lines(&cur_test->shell_log_lines, data);
            free(data);
        }

        if (pollfds[POLL_VAL].revents & POLLIN) {
            char *data = flush_pipe(val_pipe[0]);
            add_lines(&cur_test->val_lines, data);
            free(data);
        }

        if (pollfds[POLL_MFD].revents & POLLIN) {
            char *data = flush_pty(cur_test->mfd);
            write_to_term(data);
            dstrcat(&cur_test->ptydata, data);
            add_lines(&cur_test->raw_lines, data);
            free(data);
        }
    }

done:
    xclose(tfd);
    return reqdata.c_str;
}

void vterm_send(const char *msg) {
    VTerm *vtty = cur_test->vterm.tty;

    for (const char *c = msg; *c != '\0'; ++c) {
        uint32_t key = *c;

        if (*c == '\n') {
            vterm_keyboard_key(vtty, VTERM_KEY_ENTER, VTERM_MOD_NONE);
            continue;
        }

        vterm_keyboard_unichar(vtty, key, VTERM_MOD_NONE);
    }
}

void get_vt_snap(vt_snap *snap) {
    vterm_state_get_cursorpos(cur_test->vterm.st, &snap->curs_pos);

    for (int row = 0; row < cur_test->vterm.rows; ++row) {
        char **line = da_push(&snap->lines);
        *line = xcalloc(1, cur_test->vterm.cols + 1);

        for (int col = 0; col < cur_test->vterm.cols; ++col) {
            VTermScreenCell cell;
            VTermPos pos = { .row = row, .col = col };

            if (!vterm_screen_get_cell(cur_test->vterm.scr, pos, &cell))
                TEST_FAIL_MESSAGE("vterm_screen_get_cell");

            if (cell.chars[1] != 0 || cell.chars[0] > 127)
                TEST_FAIL_MESSAGE("non-ascii character in vterm");

            uint32_t ch = cell.chars[0];

            if (ch == 0)
                ch = ' ';

            (*line)[col] = ch;
        }
    }
}

void take_vt_snapshot(void) {
    vt_snap *snap = da_push(&cur_test->snaps);
    *snap = (vt_snap){0};
    get_vt_snap(snap);
}

void pty_send(const char *msg) {
    take_vt_snapshot();

    int len = (int) strlen(msg);

    if (write(cur_test->mfd, msg, len) != len)
        TEST_FAIL_MESSAGE("failed/partial write");

    /* so we don't have to call pty_ensure for every command
     * that the shell echos we'll just do it here. */
    vterm_send(msg);
    pty_ensure(msg);
}

void pty_cc(int cc_code) {
    if (write(cur_test->mfd, &tio.c_cc[cc_code], 1) != 1)
        TEST_FAIL_MESSAGE("failed/partial write");

    char *cc_out;

    switch (cc_code) {
    case VINTR: cc_out = "^C"; break;
    case VSUSP: cc_out = "^Z"; break;
    default: return;
    }

    /* so we don't have to call pty_ensure for every command
     * that the shell echos we'll just do it here. */
    pty_ensure(cc_out);
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
                "--quiet",
                "--leak-check=full",
                "--errors-for-leak-kinds=definite,indirect",
                "--show-leak-kinds=definite,indirect",
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

void new_test(void) {
    cur_test = da_push(&all_tests);
    *cur_test = (pty_test){0};

    /* init pty */
    struct winsize ws;
    if (ioctl(STDIN_FILENO, TIOCGWINSZ, &ws) == -1)
        TEST_FAIL_MESSAGE("ioctl");

    ws.ws_row = MAX(60, ws.ws_row / 2);
    ws.ws_col = MAX(30, ws.ws_col / 2);

    cur_test->cpid = xforkpty(&cur_test->mfd, NULL, &tio, &ws);
    if (cur_test->cpid == 0)
        exec_seashell();

    if (ioctl(cur_test->mfd, FIONBIO, &(int){1}) == -1)
        TEST_FAIL_MESSAGE("ioctl");

    /* init vterm */
    cur_test->vterm.tty = vterm_new(ws.ws_row, ws.ws_col);
    if (!cur_test->vterm.tty)
        TEST_FAIL_MESSAGE("vterm_new");
    cur_test->vterm.rows = ws.ws_row;
    cur_test->vterm.cols = ws.ws_col;

    vterm_set_utf8(cur_test->vterm.tty, 1);
    cur_test->vterm.scr = vterm_obtain_screen(cur_test->vterm.tty);
    if (!cur_test->vterm.scr)
        TEST_FAIL_MESSAGE("vterm_obtain_screen");
    vterm_screen_reset(cur_test->vterm.scr, 1);

    cur_test->vterm.st = vterm_obtain_state(cur_test->vterm.tty);
    if (!cur_test->vterm.st)
        TEST_FAIL_MESSAGE("vterm_obtain_state");

    /* init misc data */
    dstr_init(&cur_test->ptydata);
    cur_test->pred_i = 0;
    cur_test->test_name = strdup(Unity.CurrentTestName);
}

void new_shell(void) {
    new_test();

    pollfds[POLL_MFD].fd = cur_test->mfd;
    dstr_init(&reqdata);

    pty_ensure(PROMPT);
}

void delete_shell(void) {
    if (Unity.CurrentTestFailed != 0)
        /* so the shell can write any final data to the mfd before
         * exiting. If we don't sleep the transcript might be cut off */
        nanosleep(&(struct timespec){.tv_nsec = 25000000, .tv_sec = 0}, NULL);

    take_vt_snapshot();

    xkill(cur_test->cpid, SIGHUP);
    if (waitpid(cur_test->cpid, NULL, 0) == -1)
        TEST_FAIL_MESSAGE("waitpid failed");

    char *data = flush_pty(cur_test->mfd);
    add_lines(&cur_test->raw_lines, data);
    free(data);

    data = flush_pipe(sh_pipe[0]);
    add_lines(&cur_test->shell_log_lines, data);
    free(data);

    data = flush_pipe(val_pipe[0]);
    add_lines(&cur_test->val_lines, data);
    free(data);

    dstr_free(&reqdata);
    close(cur_test->mfd);
    vterm_free(cur_test->vterm.tty);

    cur_test->success = (Unity.CurrentTestFailed == 0);
    cur_test->val_err = (cur_test->val_lines.size > 0);
}
