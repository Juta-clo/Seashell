#include <assert.h>
#include <errno.h>
#include <ncurses.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "pty_test.h"
#include "curses_win.h"
#include "dstr.h"
#include "llog.h"
#include "proc_view.h"
#include "unity.h"
#include "xfuncs.h"

da_pty_test all_tests;

int sh_pipe[2];
int val_pipe[2];
dstr unity_buf;

struct termios tio;
struct pollfd pollfds[NFDS];

bool first = true;
int val_test_errs = 0;

pty_test *cur_test;

int mfd;
int cpid;
dstr ptydata;
size_t pred_i;

dstr reqdata;


void test_init(void) {
    da_init(&all_tests);

    set_xfatal_handler(pty_xfatal_func);
    llog_set_sink(pty_test_sink);

    dstr_init(&unity_buf);

    xtcgetattr(STDERR_FILENO, &tio);
    tio.c_lflag &= ~ECHO; /* don't echo, non-interactive */
    tio.c_oflag &= ~OPOST; /* no post processing, \r\n -> \n */

    for (int i = 0; i < NFDS; ++i)
        pollfds[i].events = POLLIN;

    xpipe2(sh_pipe, O_NONBLOCK);
    pollfds[POLL_SHELL].fd = sh_pipe[0];

    xpipe2(val_pipe, O_NONBLOCK);
    pollfds[POLL_VAL].fd = val_pipe[0];

}

XFATAL_HANDLER(pty_xfatal_func) {
    endwin();
    fprintf(stderr, "ERROR %s: %s\n", XSYSNAME, strerror(XERRNO));
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

    dstr *line = da_push(&wins[TEST_WIN].lines);
    dstr_init(line);
    dstr_printf(line, "%s llog: %s:%d: %s\n", lvl_str, info->site->file,
            info->site->line, info->msg);
}

void unity_output_char(int c) {
    dstr_push(&unity_buf, c);
}

dstr *push_line(da_dstr *lines) {
    dstr *line = da_push(lines);
    dstr_init(line);
    return line;
}

dstr *last_line(da_dstr *lines) {
    return &lines->data[lines->size - 1];
}

void add_lines(int win_num, const char *str) {
    assert(str);

    if (strcmp(str, "") == 0)
        return;

    curses_win *win = &wins[win_num];

    if (win->lines.size == 0)
        push_line(&win->lines);

    for (const char *c = str; *c != '\0'; ++c) {
        if (*c == '\n') {
            push_line(&win->lines);
            continue;
        }

        dstr_push(last_line(&win->lines), *c);
    }
}

int mev_to_win(MEVENT *mev) {
    if (mev->x >= wins[TEST_WIN].x) {
        if (mev->y >= wins[VAL_WIN].y)
            return VAL_WIN;
        else
            return TEST_WIN;
    } else {
        if (mev->y >= wins[VAL_WIN].y)
            return LOG_WIN;
        else
            return RAW_WIN;
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

/* assumes non blocking fd */
char *flush_stream(int fd, bool break_on_eio) {
#define FLUSH_STREAM_BS 4096

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
                LOG_ERR("read: %m");
                exit(EXIT_FAILURE);
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

void pty_ensure(const char *msg) {
    size_t len = strlen(msg);
    char *pty_chars = request_chars(len, &pred_i, 1000);

    TEST_ASSERT_EQUAL_size_t(len, strlen(pty_chars));
    TEST_ASSERT_EQUAL_STRING(msg, pty_chars);
}

void child_pstat_snapshot(void) {
#define INFO_BUF_SIZE 4096
    char info_buf[INFO_BUF_SIZE];

    da_pstat pstats;
    if (child_pstat(cpid, &pstats) == -1)
        TEST_FAIL_MESSAGE("child_pstat");

    if (pstats.size > 0) {
        snprintf(info_buf, INFO_BUF_SIZE, "seashell (%d)\n", cpid);
        add_lines(TEST_WIN, info_buf);

        for (size_t i = 0; i < pstats.size; ++i) {
            add_lines(TEST_WIN, "    ");
            snprintf(info_buf, INFO_BUF_SIZE, "%s (%d)\n",
                    pstats.data[i].name, pstats.data[i].pid);
            add_lines(TEST_WIN, info_buf);
        }
    } else {
        snprintf(info_buf, INFO_BUF_SIZE, "seashell (%d) no children\n", cpid);
        add_lines(TEST_WIN, info_buf);
    }

    da_free(&pstats);
}

void flush_win(int win_num) {
    int fd;
    bool break_on_eio;

    switch (win_num) {
    case RAW_WIN:
        fd = mfd;
        break_on_eio = true;
        break;
    case LOG_WIN:
        fd = sh_pipe[0];
        break_on_eio = false;
        break;
    case VAL_WIN:
        fd = val_pipe[0];
        break_on_eio = false;
        break;
    case TEST_WIN:
        return;
    }

    char *data = flush_stream(fd, break_on_eio);
    add_lines(win_num, data);
    free(data);
}

char *request_chars(size_t nchars, size_t *index, int ms) {
    assert(index);
    assert(ptydata.len >= *index);

    if (nchars == 0) {
        LOG_WARN("requested 0 characters");
        return "";
    }

    int tfd = start_timerfd(ms);
    pollfds[POLL_TFD] = (struct pollfd){.fd = tfd, .events = POLLIN};

    while (true) {
        if (ptydata.len - *index >= nchars) {
            dstrncpy(&reqdata, &ptydata.c_str[*index], nchars);
            *index += nchars;
            goto done;
        }

        xpoll(pollfds, NFDS, -1);

        if (pollfds[POLL_TFD].revents & POLLIN) {
            TEST_FAIL_MESSAGE("timed out waiting for pty data");
        }

        if (pollfds[POLL_SHELL].revents & POLLIN) {
            flush_win(LOG_WIN);
        }

        if (pollfds[POLL_VAL].revents & POLLIN) {
            ++val_test_errs;
            flush_win(VAL_WIN);
        }

        if (pollfds[POLL_MFD].revents & POLLIN) {
            char *data = flush_stream(mfd, false);
            dstrcat(&ptydata, data);
            add_lines(RAW_WIN, data);

            free(data);
        }
    }

done:
    xclose(tfd);
    return reqdata.c_str;
}

void pty_send(const char *msg) {
    int len = (int) strlen(msg);

    if (write(mfd, msg, len) != len)
        TEST_FAIL_MESSAGE("failed/partial write");

    add_lines(RAW_WIN, msg);
}

void pty_cc(int cc_code) {
    if (write(mfd, &tio.c_cc[cc_code], 1) != 1)
        TEST_FAIL_MESSAGE("failed/partial write");

    char *cc_out;

    switch (cc_code) {
    case VINTR: cc_out = "^C"; break;
    case VSUSP: cc_out = "^Z"; break;
    default: return;
    }

    add_lines(RAW_WIN, cc_out);
}

void new_shell(void) {
    dstr_init(&ptydata);
    dstr_init(&reqdata);
    pred_i = 0;

    cpid = xforkpty(&mfd, NULL, &tio, NULL);
    pollfds[POLL_MFD].fd = mfd;

    if (cpid == 0) {

        char *val_lfd_arg;
        if (asprintf(&val_lfd_arg, "--log-fd=%d", val_pipe[1]) < 0)
            TEST_FAIL_MESSAGE("asprintf: input output error");

        char *lfd_arg;
        if (asprintf(&lfd_arg, "--logfd=%d", sh_pipe[1]) < 0)
            TEST_FAIL_MESSAGE("asprintf: input output error");

        char *argv[] = {
            // "valgrind",
            //     "--vgdb=no",
            //     "--quiet",
            //     "--leak-check=full",
            //     "--errors-for-leak-kinds=definite,indirect",
            //     "--show-leak-kinds=definite,indirect",
            //     val_lfd_arg,

            "/home/juta/Projects/Seashell/seashell",
                "--prompt=simple",
                lfd_arg,
            NULL
        };

        execvp(argv[0], argv);
        _exit(EXIT_FAILURE);
    }

    if (ioctl(mfd, FIONBIO, &(int){1}) == -1) {
        LOG_ERR("ioctl");
        TEST_FAIL_MESSAGE("ioctl");
    }

    for (int win_i = 0; win_i < MAX_WIN; ++win_i) {
        curses_win *win = &wins[win_i];

        if (!first) {
            if (win_i == RAW_WIN)
                add_lines(win_i, "\n");

            add_lines(win_i, "\n");
        }

        for (int y = 0 ; y < win->ncols - 1; ++y)
            add_lines(win_i, "-");

        add_lines(win_i, "\n");
        add_lines(win_i, Unity.CurrentTestName);
        add_lines(win_i, "\n");

        for (int y = 0 ; y < win->ncols - 1; ++y)
            add_lines(win_i, "-");

        add_lines(win_i, "\n");
        add_lines(win_i, "\n");
    }

    pty_ensure(PROMPT);
}

void delete_shell(void) {
    xkill(cpid, SIGHUP);

    if (waitpid(cpid, NULL, 0) == -1)
        TEST_FAIL_MESSAGE("waitpid failed");

    flush_win(LOG_WIN);
    flush_win(RAW_WIN);

    dstr_free(&ptydata);
    dstr_free(&reqdata);
    close(mfd);

    first = false;
}
