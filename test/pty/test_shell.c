#define _GNU_SOURCE

#include <assert.h>
#include <sys/timerfd.h>
#include <sys/poll.h>
#include <unistd.h>
#include <pty.h>
#include <sys/wait.h>

#include "unity_fixture.h"
#include "pty_test.h"

TEST_GROUP(shell);

/************ Fixture ************/

TEST_SETUP(shell) {
    new_shell();
}

TEST_TEAR_DOWN(shell) {
    delete_shell();
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
    pty_ensure("[1] Started\n");
    pty_ensure(PROMPT);
    pty_ensure("\n[1] Done - Exited with status 0\n");
    pty_ensure(PROMPT);
}

TEST(shell, short_lived_bg_cmd) {
    pty_send("/bin/echo x &\n");
    pty_ensure("[1] Started\n");
    pty_ensure(PROMPT);
    pty_ensure("x\n\n[1] Done - Exited with status 0\n");
    pty_ensure(PROMPT);
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
    pty_ensure("[1] Stopped\n");
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

    pty_ensure("[1] Done - Exited with status 0\n");
    /* shouldn't see prompt between exit messages */
    pty_ensure("[2] Done - Exited with status 0\n");
}

TEST(shell, builtin_in_bg) {
    pty_send("jobs &\n");

    pty_ensure("[1] Started\n");
    pty_ensure(PROMPT);
    pty_ensure("jobs: no job control in this shell\n");
    pty_ensure("\n[1] Done - Exited with status 1\n");
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
    pty_ensure("\n[1] Done - Exited with status 0\n");
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
    pty_ensure("\n[1] Done - Exited with status 0\n");
    pty_ensure(PROMPT);

    pty_send("false && /bin/echo x &\n");
    pty_ensure("[1] Started\n");
    pty_ensure(PROMPT);
    pty_ensure("\n[1] Done - Exited with status 1\n");
    pty_ensure(PROMPT);
}

TEST(shell, or_if_logic_bg) {
    pty_send("false || /bin/echo y &\n");
    pty_ensure("[1] Started\n");
    pty_ensure(PROMPT);
    pty_ensure("y\n");
    pty_ensure("\n[1] Done - Exited with status 0\n");
    pty_ensure(PROMPT);

    pty_send("true || /bin/echo y &\n");
    pty_ensure("[1] Started\n");
    pty_ensure(PROMPT);
    pty_ensure("\n[1] Done - Exited with status 0\n");
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
    pty_ensure("   sleep 100 Running\n");
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
    pty_ensure("\n[1] Stopped\n");
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
    pty_ensure("   sleep 100 Running\n");
    pty_ensure(PROMPT);

    pty_send("kill %1\n");
    pty_ensure("[1] Done - Terminated by signal 15\n");
    pty_ensure(PROMPT);

    pty_send("jobs\n");
    pty_ensure(PROMPT);
}

TEST(shell, bg_on_stopped_job_resumes_it) {
    pty_send("sleep 100 &\n");
    pty_ensure("[1] Started\n");
    pty_ensure(PROMPT);

    pty_send("kill -STOP %1\n");
    pty_ensure("[1] Stopped\n");
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
    pty_ensure("[1] Done - Terminated by signal 15\n");
    pty_ensure(PROMPT);
}

TEST(shell, sigint_on_non_final_proc_prints_newline) {
    pty_send("self_kill -INT | self_kill -TERM\n");

    pty_ensure("\n");
    pty_ensure("[1] Done - Terminated by signal 15\n");
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

    pty_ensure("\n[1] Done - Exited with status 0\n");
    pty_ensure(PROMPT);
}

TEST(shell, fg_process_signaled) {
    pty_send("self_kill -TERM\n");
    pty_ensure("[1] Done - Terminated by signal 15\n");
    pty_ensure(PROMPT);
}

TEST(shell, fg_process_stopped) {
    pty_send("self_kill -STOP\n");
    pty_ensure("[1] Stopped\n");
    pty_ensure(PROMPT);
}

TEST(shell, fg_process_stopped_interactively) {
    pty_send("self_kill -TSTP\n");
    pty_ensure("\n");
    pty_ensure("[1] Stopped\n");
    pty_ensure(PROMPT);
}

TEST(shell, send_sigint_to_process_that_catches_it) {
    pty_send("sleep 100 | write_sleep\n");
    pty_ensure("x\n");

    child_pstat_snapshot();

    pty_cc(VINTR);
    pty_ensure("\n");
    pty_ensure(PROMPT);
}

TEST(shell, stopped_process_then_kill_builtin) {
    pty_send("self_kill -STOP | sleep 100 | write_sleep &\n");
    pty_ensure("[1] Started\n");
    pty_ensure(PROMPT);

    /* sleep a bit to let self_kill stop itself.
     * If this test fails the sleep wasn't long enough.
     *
     * Running under valgrind this will probably timeout */
    if (nanosleep(&(struct timespec){0, 5000000}, NULL) == -1)
        LOG_ERR("nanosleep: %m");

    /* make sure every process is in the pgroup */
    pty_ensure("x\n");

    pty_send("kill %1\n");
    pty_ensure("[1] Stopped\n");
    pty_ensure(PROMPT);
}

TEST(shell, stop_pline_with_plines_left) {
    pty_send("sleep 1 || true &\n");
    pty_ensure("[1] Started\n");
    pty_ensure(PROMPT);

    pty_send("kill -STOP %1\n");
    pty_ensure("[1] Stopped\n");
    pty_ensure(PROMPT);

    pty_send("kill -CONT %1\n");
    pty_ensure("[1] Continued\n");
    pty_ensure(PROMPT);

    pty_send("kill %1\n");
    pty_ensure("[1] Done - Terminated by signal 15\n");
    pty_ensure(PROMPT);
}

TEST(shell, cancel_andor_chain_with_interupt) {
    pty_send("cat && echo x\n");
    pty_send("x\n");
    pty_ensure("x\n");
    pty_cc(VINTR);
    pty_ensure("\n");
    pty_ensure(PROMPT);
}

TEST(shell, cancel_andor_chain_with_signal) {
    pty_send("self_kill -TERM || echo x\n");
    pty_ensure("[1] Done - Terminated by signal 15\n");
    pty_ensure(PROMPT);
}

/************ Test runner ************/

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
    RUN_TEST_CASE(shell, stopped_process_then_kill_builtin);
    RUN_TEST_CASE(shell, stop_pline_with_plines_left);
    RUN_TEST_CASE(shell, cancel_andor_chain_with_interupt);
    RUN_TEST_CASE(shell, cancel_andor_chain_with_signal);
}
