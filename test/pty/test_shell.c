#define _GNU_SOURCE

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
#include "dstr.h"
#include "pty_test.h"
#include "proc_view.h"

TEST_GROUP(shell);

/************ Shared utils ************/
#define PROMPT "> "

#define NFDS 3
#define POLL_MFD 0
#define POLL_SHELL 1
#define POLL_TFD 2

static dstr ptydata;
static dstr reqdata;

static size_t pred_i;

static bool first = true;
static int cpid;
static int mfd;
static struct pollfd pollfds[NFDS];

static void child_pstat_snapshot(void) {
#define INFO_BUF_SIZE 4096
    char info_buf[INFO_BUF_SIZE];

    da_pstat pstats;
    if (child_pstat(cpid, &pstats) == -1)
        TEST_FAIL_MESSAGE("child_pstat");

    if (pstats.size > 0) {
        snprintf(info_buf, INFO_BUF_SIZE, "seashell (%d)\n", cpid);
        add_lines(JOB_WIN, info_buf);

        for (size_t i = 0; i < pstats.size; ++i) {
            add_lines(JOB_WIN, "    ");
            snprintf(info_buf, INFO_BUF_SIZE, "%s (%d)\n",
                    pstats.data[i].name, pstats.data[i].pid);
            add_lines(JOB_WIN, info_buf);
        }
    } else {
        snprintf(info_buf, INFO_BUF_SIZE, "seashell (%d) no children\n", cpid);
        add_lines(JOB_WIN, info_buf);
    }

    da_free(&pstats);
}

static void flush_win(int win_num) {
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
    case JOB_WIN:
        return;
    case TEST_WIN:
        return;
    }

    char *data = flush_stream(fd, break_on_eio);
    add_lines(win_num, data);
    free(data);
}

static char *request_chars(size_t nchars, size_t *index, int ms) {
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

static void pty_ensure(const char *msg) {
    size_t len = strlen(msg);
    char *pty_chars = request_chars(len, &pred_i, 1000);

    TEST_ASSERT_EQUAL_size_t(len, strlen(pty_chars));
    TEST_ASSERT_EQUAL_STRING(msg, pty_chars);
}

static void pty_send(const char *msg) {
    int len = (int) strlen(msg);

    if (write(mfd, msg, len) != len)
        TEST_FAIL_MESSAGE("failed/partial write");

    add_lines(RAW_WIN, msg);
}

static void pty_cc(int cc_code) {
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

/************ Fixture ************/

TEST_SETUP(shell) {
    dstr_init(&ptydata);
    dstr_init(&reqdata);
    pred_i = 0;

    cpid = xforkpty(&mfd, NULL, &tio, NULL);

    if (cpid == 0) {
        char *bin = "/home/juta/Projects/Seashell/seashell";

        char *prompt_arg;
        if (asprintf(&prompt_arg, "--prompt=simple") < 0)
            TEST_FAIL_MESSAGE("asprintf: input output error");

        char *lfd_arg;
        if (asprintf(&lfd_arg, "--logfd=%d", sh_pipe[1]) < 0)
            TEST_FAIL_MESSAGE("asprintf: input output error");

        char *argv[] = { bin, prompt_arg, lfd_arg, NULL };

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
    pollfds[POLL_SHELL].fd = sh_pipe[0];

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

TEST_TEAR_DOWN(shell) {
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

/************ Tests ************/

TEST(shell, prompt_after_reclaim_tty) {
    pty_send("/bin/echo x\n");
    pty_ensure("x\n" PROMPT);
}

TEST(shell, prompt_after_launch_bg_cmd) {
    pty_send("sleep 100 &\n");
    pty_ensure("[1] Started\n" PROMPT);
}

TEST(shell, prompt_after_bg_job_done) {
    pty_send("sleep 0.05 &\n");
    pty_ensure("[1] Started\n" PROMPT "\n[1] Exited with status 0\n" PROMPT);
}

TEST(shell, short_lived_bg_cmd) {
    pty_send("/bin/echo x &\n");
    pty_ensure("[1] Started\n" PROMPT "x\n\n[1] Exited with status 0\n" PROMPT);
}

TEST(shell, int_fg_job) {
    pty_send("cat\nhi\n");
    pty_ensure("hi\n");

    pty_cc(VINTR);

    pty_ensure("\n");
    pty_ensure(PROMPT);
}

TEST(shell, stop_fg_job) {
    pty_send("cat\nhi\n");
    pty_ensure("hi\n");

    pty_cc(VSUSP);

    pty_ensure("\n");
    pty_ensure("[1] Stopped by signal 20\n");
    pty_ensure(PROMPT);
}

TEST(shell, bg_jobs_pid_state) {
    pty_send("sleep 100 &\n");
    pty_ensure("[1] Started\n");
    pty_ensure(PROMPT);

    pty_send("sleep 100 &\n");
    pty_ensure("[2] Started\n");
    pty_ensure(PROMPT);
}

TEST(shell, unknown_cmd) {
    pty_send("zoobar\n"); /* hopefully no-one ever makes this... */

    pty_ensure("seashell: command not found: zoobar\n");
    pty_ensure(PROMPT);
}

TEST(shell, pipeline) {
    pty_send("/bin/echo hi | grep h | wc -c\n");
        /* wc outputs 3 because it includes the newline from grep */
    pty_ensure("3\n");
    pty_ensure(PROMPT);
}

TEST(shell, bg_job_done_with_fg_job) {

    /* launch 2 bg jobs to determine if the prompt was not printed.
     * If the prompt does not print between job exit messages it was
     * correctly skipped.
     *
     * flush_pty should also have no output. */

    pty_send("sleep 0.05 &\n");
    pty_ensure("[1] Started\n");
    pty_ensure(PROMPT);

    pty_send("sleep 0.1 &\n");
    pty_ensure("[2] Started\n");
    pty_ensure(PROMPT);

    pty_send("sleep 1\n"); /* lose fgroup control */

    child_pstat_snapshot();

    pty_ensure("[1] Exited with status 0\n");
    /* shouldn't see prompt between exit messages */
    pty_ensure("[2] Exited with status 0\n");
}

TEST(shell, builtin_in_bg) {
    pty_send("jobs &\n");

    pty_ensure("[1] Started\n");
    pty_ensure(PROMPT);
    pty_ensure("jobs: no job control in this shell\n");
    pty_ensure("\n[1] Exited with status 1\n");
    pty_ensure(PROMPT);
}

TEST(shell, pipeline_of_builtins_does_not_duplicate_prompt) {

    /* Regression: after adding display_prompt() to try_run_builtin()
     * and neglecting to confirm if we are in the fg, and not calling
     * from a subshell, the prompt was duplicated. */

    pty_send("jobs | cd .\n");

    pty_ensure("jobs: no job control in this shell\n");
    pty_ensure(PROMPT);
}

TEST(shell, simple_andor_chain) {
    pty_send("true && /bin/echo x\n");

    pty_ensure("x\n");
    pty_ensure(PROMPT);
}

TEST(shell, simple_bg_andor_chain) {
    pty_send("true && /bin/echo x &\n");

    pty_ensure("[1] Started\n");
    pty_ensure(PROMPT);
    pty_ensure("x\n");
    pty_ensure("\n[1] Exited with status 0\n");
    pty_ensure(PROMPT);
}

TEST(shell, and_if_logic) {
    pty_send("true && /bin/echo x\n");
    pty_ensure("x\n");
    pty_ensure(PROMPT);

    pty_send("false && /bin/echo x\n");
    pty_ensure(PROMPT);
}

TEST(shell, or_if_logic) {
    pty_send("false || /bin/echo y\n");
    pty_ensure("y\n");
    pty_ensure(PROMPT);

    pty_send("true || /bin/echo y\n");
    pty_ensure(PROMPT);
}

TEST(shell, and_if_logic_bg) {
    pty_send("true && /bin/echo x &\n");
    pty_ensure("[1] Started\n");
    pty_ensure(PROMPT);
    pty_ensure("x\n");
    pty_ensure("\n[1] Exited with status 0\n");
    pty_ensure(PROMPT);

    pty_send("false && /bin/echo x &\n");
    pty_ensure("[1] Started\n");
    pty_ensure(PROMPT);
    pty_ensure("\n[1] Exited with status 1\n");
    pty_ensure(PROMPT);
}

TEST(shell, or_if_logic_bg) {
    pty_send("false || /bin/echo y &\n");
    pty_ensure("[1] Started\n");
    pty_ensure(PROMPT);
    pty_ensure("y\n");
    pty_ensure("\n[1] Exited with status 0\n");
    pty_ensure(PROMPT);

    pty_send("true || /bin/echo y &\n");
    pty_ensure("[1] Started\n");
    pty_ensure(PROMPT);
    pty_ensure("\n[1] Exited with status 0\n");
    pty_ensure(PROMPT);
}

TEST(shell, and_or_chain_with_builtins) {
    pty_send("echo x && echo y\n");

    pty_ensure("x\n");
    pty_ensure("y\n");
    pty_ensure(PROMPT);
}

TEST(shell, prompt_after_launch_fg_builtin) {
    pty_send("cd .\n");

    pty_ensure(PROMPT);
}

TEST(shell, jobs_builtin) {
    pty_send("jobs\n");
    pty_ensure(PROMPT);

    pty_send("sleep 100 &\n");
    pty_ensure("[1] Started\n");
    pty_ensure(PROMPT);

    pty_send("jobs\n");
    pty_ensure("[1] Running\n");
    pty_ensure("   'sleep 100' Running\n");
    pty_ensure(PROMPT);
}

TEST(shell, builtin_and_or_logic) {
    pty_send("cd /non-existant || echo ?\n");
    pty_ensure("cd: chdir: No such file or directory\n");
    pty_ensure("?\n");
    pty_ensure(PROMPT);

    pty_send("cd /non-existant && echo ?\n");
    pty_ensure("cd: chdir: No such file or directory\n");
    pty_ensure(PROMPT);
}

TEST(shell, fg_stopped_bg_job) {
    pty_send("cat &\n");
    pty_ensure("[1] Started\n");
    pty_ensure(PROMPT);
    pty_ensure("\n[1] Stopped (tty input) by signal 21\n");
    pty_ensure(PROMPT);

    pty_send("fg %1\n");
    pty_ensure("[1] Continued\n");
}

TEST(shell, kill_builtin_works_on_bg_jobs) {
    pty_send("sleep 100 &\n");
    pty_ensure("[1] Started\n");
    pty_ensure(PROMPT);

    pty_send("jobs\n");
    pty_ensure("[1] Running\n");
    pty_ensure("   'sleep 100' Running\n");
    pty_ensure(PROMPT);

    child_pstat_snapshot();

    pty_send("kill %1\n");
    pty_ensure("[1] Terminated by signal 15\n");
    pty_ensure(PROMPT);

    child_pstat_snapshot();

    pty_send("jobs\n");
    pty_ensure(PROMPT);
}

TEST(shell, bg_on_stopped_job_resumes_it) {
    pty_send("sleep 100 &\n");
    pty_ensure("[1] Started\n");
    pty_ensure(PROMPT);

    pty_send("kill -STOP %1\n");
    pty_ensure("[1] Stopped (signal) by signal 19\n");
    pty_ensure(PROMPT);

    pty_send("bg %1\n");
    pty_ensure("[1] Continued\n");
    pty_ensure(PROMPT);
}

TEST(shell, kill_multiple_proc_job) {
    pty_send("sleep 100 | sleep 100 | write_sleep &\n");
    pty_ensure("[1] Started\n");
    pty_ensure(PROMPT);

    pty_ensure("x\n");

    pty_send("kill %1\n");
    pty_ensure("[1] Terminated by signal 15\n");
    pty_ensure(PROMPT);
}

TEST(shell, sigint_on_non_final_proc_prints_newline) {
    pty_send("self_kill -INT | self_kill -TERM\n");

    pty_ensure("\n");
    pty_ensure("[1] Terminated by signal 15\n");
    pty_ensure(PROMPT);
}

TEST(shell, sigint_on_final_proc) {
    pty_send("self_kill -INT\n");

    pty_ensure("\n");
    pty_ensure(PROMPT);
}

TEST(shell, process_exits_normally_in_bg) {
    pty_send("true &\n");
    pty_ensure("[1] Started\n");
    pty_ensure(PROMPT);

    pty_ensure("\n[1] Exited with status 0\n");
    pty_ensure(PROMPT);
}

TEST(shell, fg_process_signaled) {
    pty_send("self_kill -TERM\n");
    pty_ensure("[1] Terminated by signal 15\n");
    pty_ensure(PROMPT);
}

TEST(shell, fg_process_stopped) {
    pty_send("self_kill -STOP\n");
    pty_ensure("[1] Stopped (signal) by signal 19\n");
    pty_ensure(PROMPT);
}

TEST(shell, fg_process_stopped_interactively) {
    pty_send("self_kill -TSTP\n");
    pty_ensure("\n");
    pty_ensure("[1] Stopped by signal 20\n");
    pty_ensure(PROMPT);
}

TEST(shell, send_sigint_to_process_that_catches_it) {
    pty_send("sleep 100 | write_sleep\n");
    pty_ensure("x\n");

    pty_cc(VINTR);

    pty_ensure("\n");
    pty_ensure(PROMPT);
}

/************ Test runner ************/

// SIGINT on any process causes a newline to be printed. It does NOT have
// to be the last process.
//
// SIGTSTP that causes a job to transition to the stopped state causes a
// newline to be printed.
//
// The final process exiting determines the job control output message.
// - If the final process is terminated by SIGINT then we print nothing.
// - If the final process exits normally we print the exit status.
// - If the final process terminated by a signal then we print the term sig.
//
// The first process to stop determines the job control output message.
// - If the first process is stopped by SIGTSTP then we print newline + message
// - If the first process is stopped by a signal then we print the stop sig

TEST_GROUP_RUNNER(shell) {
    RUN_TEST_CASE(shell, prompt_after_reclaim_tty);
    RUN_TEST_CASE(shell, prompt_after_launch_bg_cmd);
    RUN_TEST_CASE(shell, prompt_after_bg_job_done);
    RUN_TEST_CASE(shell, short_lived_bg_cmd);
    RUN_TEST_CASE(shell, int_fg_job);
    RUN_TEST_CASE(shell, stop_fg_job);
    RUN_TEST_CASE(shell, bg_jobs_pid_state);
    RUN_TEST_CASE(shell, unknown_cmd);
    RUN_TEST_CASE(shell, pipeline);
    RUN_TEST_CASE(shell, bg_job_done_with_fg_job);
    RUN_TEST_CASE(shell, builtin_in_bg);
    RUN_TEST_CASE(shell, pipeline_of_builtins_does_not_duplicate_prompt);
    RUN_TEST_CASE(shell, simple_andor_chain);
    RUN_TEST_CASE(shell, simple_bg_andor_chain);
    RUN_TEST_CASE(shell, and_if_logic);
    RUN_TEST_CASE(shell, or_if_logic);
    RUN_TEST_CASE(shell, or_if_logic_bg);
    RUN_TEST_CASE(shell, and_if_logic_bg);
    RUN_TEST_CASE(shell, and_or_chain_with_builtins);
    RUN_TEST_CASE(shell, prompt_after_launch_fg_builtin);
    RUN_TEST_CASE(shell, jobs_builtin);
    RUN_TEST_CASE(shell, builtin_and_or_logic);
    RUN_TEST_CASE(shell, fg_stopped_bg_job);
    RUN_TEST_CASE(shell, kill_builtin_works_on_bg_jobs);
    RUN_TEST_CASE(shell, bg_on_stopped_job_resumes_it);
    RUN_TEST_CASE(shell, kill_multiple_proc_job);
    RUN_TEST_CASE(shell, sigint_on_non_final_proc_prints_newline);
    RUN_TEST_CASE(shell, sigint_on_final_proc);
    RUN_TEST_CASE(shell, sigint_on_final_proc);
    RUN_TEST_CASE(shell, process_exits_normally_in_bg);
    RUN_TEST_CASE(shell, fg_process_signaled);
    RUN_TEST_CASE(shell, fg_process_stopped);
    RUN_TEST_CASE(shell, fg_process_stopped_interactively);
    RUN_TEST_CASE(shell, send_sigint_to_process_that_catches_it);
}
