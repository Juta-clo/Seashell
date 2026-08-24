#define _GNU_SOURCE

#include <errno.h>
#include <assert.h>
#include <sys/timerfd.h>
#include <time.h>
#include <string.h>
#include <sys/poll.h>
#include <stdlib.h>
#include <unistd.h>
#include <pty.h>
#include <sys/wait.h>

#include "unity_fixture.h"
#include "xfuncs.h"
#include "pty_test.h"

TEST_GROUP(prompt);

/************ Shared utils ************/

#define PROMPT "> "

#define NFDS 3
#define POLL_MFD 0
#define POLL_SHELL 1
#define POLL_TFD 2

static bool first = true;
static size_t ptybuf_i;
static size_t pred_i;
static char ptybuf[PTY_RBUF];
static char outbuf[PTY_RBUF];

static int cpid;
static int mfd;
static struct termios tio;
static struct pollfd pollfds[NFDS];

static void flush_win(int win_num) {
    int fd;
    bool break_on_eio;

    switch (win_num) {
    case RAW_WIN:
        fd = mfd;
        break_on_eio = true;
        break;
    case LOG_WIN:
        fd = seashell_pipe[0];
        break_on_eio = false;
        break;
    case UNITY_WIN:
        return;
    case HARNESS_WIN:
        return;
    }

    char *data = flush_stream(fd, break_on_eio);
    xwprintw(wins[win_num].win, "%s", data);
    xwrefresh(wins[win_num].win);
    free(data);
}

static char *request_chars(size_t nchars, size_t *index, int ms) {
    assert(index);
    assert(ptybuf_i >= *index);

    if (nchars == 0)
        return "";

    int sec = ms / 1000;
    ms %= 1000;

    int ns = ms * 1000000;

    struct itimerspec its = {
        .it_value = {.tv_sec = sec, .tv_nsec = ns},
        .it_interval = {0}
    };

    int tfd = xtimerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    xtimerfd_settime(tfd, 0, &its, NULL);

    pollfds[POLL_TFD] = (struct pollfd){.fd = tfd, .events = POLLIN};

    bool done = false;

    while (true) {
        if (ptybuf_i - *index >= nchars) {
            strncpy(outbuf, &ptybuf[*index], nchars);
            outbuf[nchars] = '\0';
            *index += nchars;
            xclose(tfd);
            return outbuf;
        }

        if (done)
            break;

        xpoll(pollfds, NFDS, -1);

        if (pollfds[POLL_MFD].revents & POLLIN) {
            int num_read = read(mfd, &ptybuf[ptybuf_i], PTY_RBUF - 1 - ptybuf_i);
            if (num_read == -1 && errno != EAGAIN) {
                LOG_ERR("read: %m");
                TEST_FAIL_MESSAGE("read");
            }

            strncpy(outbuf, &ptybuf[ptybuf_i], num_read);
            outbuf[num_read] = '\0';
            xwprintw(wins[RAW_WIN].win, "%s", outbuf);
            xwrefresh(wins[RAW_WIN].win);

            ptybuf_i += num_read;
        }

        if (pollfds[POLL_SHELL].revents & POLLIN) {
            flush_win(LOG_WIN);
        }

        if (pollfds[POLL_TFD].revents & POLLIN) {
            done = true;
        }
    }

    xclose(tfd);
    return NULL;
}

static void pty_ensure(const char *msg) {
    LOG_INFO("msg=%s", msg);

    size_t len = strlen(msg);

    char *pty_chars = request_chars(len, &pred_i, 1000);
    if (!pty_chars) {
        LOG_ERR("request_chars(.len=%d, .pred_i=%d, .ms=%d)",
                len, pred_i, 1000);
        TEST_FAIL_MESSAGE("request_chars");
    }

    TEST_ASSERT_EQUAL_size_t(len, strlen(pty_chars));
    TEST_ASSERT_EQUAL_STRING(msg, pty_chars);
}

static void pty_send(const char *msg) {
    int len = (int) strlen(msg);

    if (write(mfd, msg, len) != len)
        TEST_FAIL_MESSAGE("failed/partial write");

    xwprintw(wins[RAW_WIN].win, "%s", msg);
    xwrefresh(wins[RAW_WIN].win);
}

static void pty_cc(int cc_code) {
    char cc_str[2] = { tio.c_cc[cc_code], '\0' };

    if (write(mfd, cc_str, 2) != 2)
        TEST_FAIL_MESSAGE("failed/partial write");

    char *cc_out;

    switch (cc_code) {
    case VINTR: cc_out = "^C"; break;
    case VSUSP: cc_out = "^Z"; break;
    default: return;
    }

    xwprintw(wins[RAW_WIN].win, "%s", cc_out);
    xwrefresh(wins[RAW_WIN].win);
}

/************ Fixture ************/

TEST_SETUP(prompt) {
    pred_i = 0;
    ptybuf_i = 0;
    ptybuf[0] = '\0';
    outbuf[0] = '\0';

    cpid = xforkpty(&mfd, NULL, &tio, NULL);

    if (cpid == 0) {
        char *bin = "/home/juta/Projects/Seashell/seashell";

        char *lfd_arg;
        if (asprintf(&lfd_arg, "--logfd=%d", seashell_pipe[1]) < 0)
            TEST_FAIL_MESSAGE("asprintf: input output error");

        char *argv[] = { bin, lfd_arg, NULL };

        execvp(argv[0], argv);
        _exit(EXIT_FAILURE);
    }

    if (ioctl(mfd, FIONBIO, &(int){1}) == -1) {
        LOG_ERR("ioctl");
        TEST_FAIL_MESSAGE("ioctl");
    }

    pollfds[POLL_MFD].events = POLLIN;
    pollfds[POLL_MFD].fd = mfd;

    pollfds[POLL_SHELL].events = POLLIN;
    pollfds[POLL_SHELL].fd = seashell_pipe[0];

    for (int i = HARNESS_WIN; i <= LOG_WIN; ++i) {
        if (!first) {
            if (i == RAW_WIN)
                xwprintw(wins[i].win, "\n");
            xwprintw(wins[i].win, "\n");
        }

        for (int y = 0 ; y < wins[i].ncols - 1; ++y)
            xwcprintw(wins[i].win, CP_MAGENTA_CL, "-");

        xwcprintw(wins[i].win, CP_MAGENTA_CL, " %s\n", Unity.CurrentTestName);

        for (int y = 0 ; y < wins[i].ncols - 1; ++y)
            xwcprintw(wins[i].win, CP_MAGENTA_CL, "-");

        xwprintw(wins[i].win, "\n\n");
        xwrefresh(wins[i].win);
    }

    pty_ensure(PROMPT);
}

TEST_TEAR_DOWN(prompt) {
    xkill(cpid, SIGHUP);

    if (waitpid(cpid, NULL, 0) == -1)
        TEST_FAIL_MESSAGE("waitpid failed");

    flush_win(LOG_WIN);
    flush_win(RAW_WIN);

    close(mfd);
}

/************ Tests ************/

TEST(prompt, prompt_after_reclaim_tty) {
    pty_send("/bin/echo x\n");
    pty_ensure("x\n" PROMPT);
}

TEST(prompt, prompt_after_launch_bg_cmd) {
    pty_send("sleep 100 &\n");
    pty_ensure("[1] started\n" PROMPT);
}

TEST(prompt, prompt_after_bg_job_done) {
    pty_send("sleep 0.05 &\n");
    pty_ensure("[1] started\n" PROMPT "\n[1] exited\n" PROMPT);
}

TEST(prompt, short_lived_bg_cmd) {
    pty_send("/bin/echo x &\n");
    pty_ensure("[1] started\n" PROMPT "x\n\n[1] exited\n" PROMPT);
}

TEST(prompt, int_fg_job) {
    pty_send("cat\nhi\n");
    pty_ensure("hi\n");

    pty_cc(VINTR);

    pty_ensure("\n");
    pty_ensure(PROMPT);
}

TEST(prompt, stop_fg_job) {
    pty_send("cat\nhi\n");
    pty_ensure("hi\n");

    pty_cc(VSUSP);

    pty_ensure("\n");
    pty_ensure("[1] Stopped by signal 20\n");
    pty_ensure(PROMPT);
}

TEST(prompt, bg_jobs_pid_state) {
    pty_send("sleep 100 &\n");
    pty_ensure("[1] started\n");
    pty_ensure(PROMPT);

    pty_send("sleep 100 &\n");
    pty_ensure("[2] started\n");
    pty_ensure(PROMPT);
}

TEST(prompt, unknown_cmd) {
    pty_send("zoobar\n"); /* hopefully no-one ever makes this... */

    pty_ensure("seashell: command not found: zoobar\n");
    pty_ensure(PROMPT);
}

TEST(prompt, pipeline) {
    pty_send("/bin/echo hi | grep h | wc -c\n");
        /* wc outputs 3 because it includes the newline from grep */
    pty_ensure("3\n");
    pty_ensure(PROMPT);
}

TEST(prompt, bg_job_done_with_fg_job) {

    /* launch 2 bg jobs to determine if the prompt was not printed.
     * If the prompt does not print between job exit messages it was
     * correctly skipped.
     *
     * flush_pty should also have no output. */

    pty_send("sleep 0.05 &\n");
    pty_ensure("[1] started\n");
    pty_ensure(PROMPT);

    pty_send("sleep 0.1 &\n");
    pty_ensure("[2] started\n");
    pty_ensure(PROMPT);

    pty_send("sleep 1\n"); /* lose fgroup control */

    pty_ensure("[1] exited\n");
    /* shouldn't see prompt between exit messages */
    pty_ensure("[2] exited\n");
}

TEST(prompt, builtin_in_bg) {
    pty_send("jobs &\n");

    pty_ensure("[1] started\n");
    pty_ensure(PROMPT);
    pty_ensure("jobs: no job control in this shell\n");
    pty_ensure("\n[1] exited\n");
    pty_ensure(PROMPT);
}

TEST(prompt, pipeline_of_builtins_does_not_duplicate_prompt) {

    /* Regression: after adding display_prompt() to try_run_builtin()
     * and neglecting to confirm if we are in the fg, and not calling
     * from a subshell, the prompt was duplicated. */

    pty_send("jobs | cd .\n");

    pty_ensure("jobs: no job control in this shell\n");
    pty_ensure(PROMPT);
}

TEST(prompt, simple_andor_chain) {
    pty_send("true && /bin/echo x\n");

    pty_ensure("x\n");
    pty_ensure(PROMPT);
}

TEST(prompt, simple_bg_andor_chain) {
    pty_send("true && /bin/echo x &\n");

    pty_ensure("[1] started\n");
    pty_ensure(PROMPT);
    pty_ensure("x\n");
    pty_ensure("\n[1] exited\n");
    pty_ensure(PROMPT);
}

TEST(prompt, and_if_logic) {
    pty_send("true && /bin/echo x\n");
    pty_ensure("x\n");
    pty_ensure(PROMPT);

    pty_send("false && /bin/echo x\n");
    pty_ensure(PROMPT);
}

TEST(prompt, or_if_logic) {
    pty_send("false || /bin/echo y\n");
    pty_ensure("y\n");
    pty_ensure(PROMPT);

    pty_send("true || /bin/echo y\n");
    pty_ensure(PROMPT);
}

TEST(prompt, and_if_logic_bg) {
    pty_send("true && /bin/echo x &\n");
    pty_ensure("[1] started\n");
    pty_ensure(PROMPT);
    pty_ensure("x\n");
    pty_ensure("\n[1] exited\n");
    pty_ensure(PROMPT);

    pty_send("false && /bin/echo x &\n");
    pty_ensure("[1] started\n");
    pty_ensure(PROMPT);
    pty_ensure("\n[1] exited\n");
    pty_ensure(PROMPT);
}

TEST(prompt, or_if_logic_bg) {
    pty_send("false || /bin/echo y &\n");
    pty_ensure("[1] started\n");
    pty_ensure(PROMPT);
    pty_ensure("y\n");
    pty_ensure("\n[1] exited\n");
    pty_ensure(PROMPT);

    pty_send("true || /bin/echo y &\n");
    pty_ensure("[1] started\n");
    pty_ensure(PROMPT);
    pty_ensure("\n[1] exited\n");
    pty_ensure(PROMPT);
}

TEST(prompt, and_or_chain_with_builtins) {
    pty_send("echo x && echo y\n");

    pty_ensure("x\n");
    pty_ensure("y\n");
    pty_ensure(PROMPT);
}

TEST(prompt, prompt_after_launch_fg_builtin) {
    pty_send("cd .\n");

    pty_ensure(PROMPT);
}

TEST(prompt, jobs_builtin) {
    pty_send("jobs\n");
    pty_send("sleep 100 &\n");
    pty_send("jobs\n");

    pty_ensure(PROMPT);
    pty_ensure("[1] started\n");
    pty_ensure(PROMPT);
    pty_ensure("[1] running\n");
    pty_ensure(PROMPT);
}

TEST(prompt, builtin_and_or_logic) {
    pty_send("cd /non-existant || echo ?\n");
    pty_ensure("cd: chdir: No such file or directory\n");
    pty_ensure("?\n");
    pty_ensure(PROMPT);

    pty_send("cd /non-existant && echo ?\n");
    pty_ensure("cd: chdir: No such file or directory\n");
    pty_ensure(PROMPT);
}

/************ Test runner ************/

TEST_GROUP_RUNNER(prompt) {
    xtcgetattr(STDERR_FILENO, &tio);
    tio.c_lflag &= ~ECHO; /* don't echo, non-interactive */
    tio.c_oflag &= ~OPOST; /* no post processing, \r\n -> \n */

    RUN_TEST_CASE(prompt, prompt_after_reclaim_tty);

    first = false;

    RUN_TEST_CASE(prompt, prompt_after_launch_bg_cmd);
    RUN_TEST_CASE(prompt, prompt_after_bg_job_done);
    RUN_TEST_CASE(prompt, short_lived_bg_cmd);
    RUN_TEST_CASE(prompt, int_fg_job);
    RUN_TEST_CASE(prompt, stop_fg_job);
    RUN_TEST_CASE(prompt, bg_jobs_pid_state);
    RUN_TEST_CASE(prompt, unknown_cmd);
    RUN_TEST_CASE(prompt, pipeline);
    RUN_TEST_CASE(prompt, bg_job_done_with_fg_job);
    RUN_TEST_CASE(prompt, builtin_in_bg);
    RUN_TEST_CASE(prompt, pipeline_of_builtins_does_not_duplicate_prompt);
    RUN_TEST_CASE(prompt, simple_andor_chain);
    RUN_TEST_CASE(prompt, simple_bg_andor_chain);
    RUN_TEST_CASE(prompt, and_if_logic);
    RUN_TEST_CASE(prompt, or_if_logic);
    RUN_TEST_CASE(prompt, or_if_logic_bg);
    RUN_TEST_CASE(prompt, and_if_logic_bg);
    RUN_TEST_CASE(prompt, and_or_chain_with_builtins);
    RUN_TEST_CASE(prompt, prompt_after_launch_fg_builtin);
    RUN_TEST_CASE(prompt, jobs_builtin);
    RUN_TEST_CASE(prompt, builtin_and_or_logic);
}
