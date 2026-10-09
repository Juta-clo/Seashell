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

/* Must ensure that the program is in the foreground group
 * before writing to the pty, else the bytes will be interpreted
 * by seashells termios settings.
 *
 * write_sleep writes 1 byte then sleeps. Verify the byte is
 * printed is enough to know that the handoff was successful. */

// NOLINTBEGIN(bugprone-suspicious-missing-comma)

TEST(shell, prompt_after_reclaim_tty) {
    vterm_send_str("/bin/echo x\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "/bin/echo x",
        "x",
        PROMPT
    );
}

TEST(shell, prompt_after_launch_bg_cmd) {
    vterm_send_str("sleep 100 &\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] Started",
        PROMPT
    );
}

TEST(shell, prompt_after_bg_job_done) {
    vterm_send_str("sleep 100 &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] Started",
        PROMPT
    );

    vterm_send_str("x");
    assert_term(0, PROMPT_LEN + 1,
        PROMPT "x"
    );

    TEST_ASSERT_MESSAGE(kill_child("sleep"), "kill_child(\"sleep\")");

    assert_term(2, PROMPT_LEN + 1,
        PROMPT "x",
        "[1] 'sleep' Terminated (15)",
        PROMPT "x"
    );
}

TEST(shell, short_lived_bg_cmd) {
    vterm_send_str("true &\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "true &",
        "[1] Started",
        PROMPT,
        "[1] 'true' Exited (0)",
        PROMPT
    );
}

TEST(shell, interupt_foreground_job) {
    vterm_send_str("write_sleep\n");
    assert_term(2, 0,
        PROMPT "write_sleep",
        "x"
    );

    vterm_send_char('c', VTERM_MOD_CTRL);
    assert_term(3, PROMPT_LEN,
        PROMPT "write_sleep",
        "x",
        "^C%",
        PROMPT
    );
}

TEST(shell, stop_fg_job) {
    vterm_send_str("write_sleep\n");
    assert_term(2, 0,
        PROMPT "write_sleep",
        "x"
    );

    vterm_send_char('z', VTERM_MOD_CTRL);
    assert_term(4, PROMPT_LEN,
        PROMPT "write_sleep",
        "x",
        "^Z",
        "[1] Stopped",
        PROMPT
    );
}

TEST(shell, bg_jobs_pid_state) {
    vterm_send_str("sleep 100 &\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] Started",
        PROMPT
    );

    vterm_send_str("sleep 100 &\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] Started",
        PROMPT "sleep 100 &",
        "[2] Started",
        PROMPT
    );
}

TEST(shell, unknown_cmd) {
    vterm_send_str("zoobar\n"); /* hopefully no-one ever makes this... */
    assert_term(2, PROMPT_LEN,
        PROMPT "zoobar",
        "seashell: command not found: zoobar",
        PROMPT
    );
}

TEST(shell, pipeline) {
    /* wc outputs 3 because it includes the newline from grep */
    vterm_send_str("/bin/echo hi | grep h | wc -c\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "/bin/echo hi | grep h | wc -c",
        "3",
        PROMPT
    );
}

TEST(shell, bg_job_done_with_fg_job) {

    /* launch 2 bg jobs to determine if the prompt was not printed.
     * If the prompt does not print between job exit messages it was
     * correctly skipped. */

    vterm_send_str("sleep 100 &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] Started",
        PROMPT
    );

    vterm_send_str("write_sleep\n"); /* lose fgroup control */
    assert_term(2, 0,
        PROMPT "write_sleep",
        "x"
    );

    TEST_ASSERT_MESSAGE(kill_child("sleep"), "kill_child(\"sleep\")");

    assert_term(3, 0,
        PROMPT "write_sleep",
        "x",
        "[1] 'sleep' Terminated (15)"
    );
}

TEST(shell, builtin_in_bg) {
    vterm_send_str("cd . &\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "cd . &",
        "[1] Started",
        PROMPT,
        "[1] 'cd' Exited (0)",
        PROMPT
    );
}

TEST(shell, pipeline_of_builtins_does_not_duplicate_prompt) {

    /* Regression: after adding display_prompt() to try_run_builtin()
     * and neglecting to confirm if we are in the fg, and not calling
     * from a subshell, the prompt was duplicated. */

    vterm_send_str("jobs | cd .\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "jobs | cd .",
        "jobs: no job control in this shell",
        PROMPT
    );
}

TEST(shell, simple_andor_chain) {
    vterm_send_str("true && /bin/echo x\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "true && /bin/echo x",
        "x",
        PROMPT
    );
}

TEST(shell, simple_bg_andor_chain) {
    vterm_send_str("true && /bin/echo x &\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "true && /bin/echo x &",
        "[1] Started",
        PROMPT "x",
        "[1] '/bin/echo' Exited (0)",
        PROMPT
    );
}

TEST(shell, and_if_logic) {
    vterm_send_str("true && /bin/echo x\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "true && /bin/echo x",
        "x",
        PROMPT
    );

    vterm_send_str("false && /bin/echo x\n");
    assert_term(1, PROMPT_LEN,
        PROMPT "false && /bin/echo x",
        PROMPT
    );
}

TEST(shell, or_if_logic) {
    vterm_send_str("false || /bin/echo y\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "false || /bin/echo y",
        "y",
        PROMPT
    );

    vterm_send_str("true || /bin/echo y\n");
    assert_term(1, PROMPT_LEN,
        PROMPT "true || /bin/echo y",
        PROMPT
    );
}

TEST(shell, and_if_logic_bg) {
    vterm_send_str("true && /bin/echo x &\n");
    assert_term_clear(4, PROMPT_LEN,
        PROMPT "true && /bin/echo x &",
        "[1] Started",
        PROMPT "x",
        "[1] '/bin/echo' Exited (0)",
        PROMPT
    );

    vterm_send_str("false && /bin/echo x &\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "false && /bin/echo x &",
        "[1] Started",
        PROMPT,
        "[1] 'false' Exited (1)",
        PROMPT
    );
}

TEST(shell, or_if_logic_bg) {
    vterm_send_str("false || /bin/echo y &\n");
    assert_term_clear(4, PROMPT_LEN,
        PROMPT "false || /bin/echo y &",
        "[1] Started",
        PROMPT "y",
        "[1] '/bin/echo' Exited (0)",
        PROMPT
    );

    vterm_send_str("true || /bin/echo y &\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "true || /bin/echo y &",
        "[1] Started",
        PROMPT,
        "[1] 'true' Exited (0)",
        PROMPT
    );
}

TEST(shell, and_or_chain_with_builtins) {
    vterm_send_str("echo x && echo y\n");
    assert_term(3, PROMPT_LEN,
        PROMPT "echo x && echo y",
        "x",
        "y",
        PROMPT
    );
}

TEST(shell, prompt_after_launch_fg_builtin) {
    vterm_send_str("cd .\n");
    assert_term(1, PROMPT_LEN,
        PROMPT "cd .",
        PROMPT
    );
}

TEST(shell, jobs_builtin) {
    vterm_send_str("jobs\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "jobs",
        PROMPT
    );

    vterm_send_str("sleep 100 &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] Started",
        PROMPT
    );

    vterm_send_str("jobs\n");
    assert_term(3, PROMPT_LEN,
        PROMPT "jobs",
        "[1] Running",
        "   sleep 100 Running",
        PROMPT
    );
}

TEST(shell, builtin_and_or_logic) {
    vterm_send_str("cd /non-existant || echo ?\n");
    assert_term_clear(3, PROMPT_LEN,
        PROMPT "cd /non-existant || echo ?",
        "cd: chdir: No such file or directory",
        "?",
        PROMPT
    );

    vterm_send_str("cd /non-existant && echo ?\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "cd /non-existant && echo ?",
        "cd: chdir: No such file or directory",
        PROMPT
    );
}

TEST(shell, fg_stopped_bg_job) {
    vterm_send_str("cat &\n");
    assert_term_clear(4, PROMPT_LEN,
        PROMPT "cat &",
        "[1] Started",
        PROMPT,
        "[1] Stopped",
        PROMPT
    );

    vterm_send_str("fg %1\n");
    assert_term(2, 0,
        PROMPT "fg %1",
        "[1] Continued"
    );
}

TEST(shell, kill_builtin_works_on_bg_jobs) {
    vterm_send_str("sleep 100 &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] Started",
        PROMPT
    );

    vterm_send_str("jobs\n");
    assert_term_clear(3, PROMPT_LEN,
        PROMPT "jobs",
        "[1] Running",
        "   sleep 100 Running",
        PROMPT
    );

    vterm_send_str("kill %1\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "kill %1",
        "[1] 'sleep' Terminated (15)",
        PROMPT
    );

    vterm_send_str("jobs\n");
    assert_term(1, PROMPT_LEN,
        PROMPT "jobs",
        PROMPT
    );
}

TEST(shell, bg_on_stopped_job_resumes_it) {
    vterm_send_str("sleep 100 &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] Started",
        PROMPT
    );

    vterm_send_str("kill -STOP %1\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "kill -STOP %1",
        "[1] Stopped",
        PROMPT
    );

    vterm_send_str("bg %1\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "bg %1",
        "[1] Continued",
        PROMPT
    );
}

TEST(shell, kill_multiple_proc_job) {
    vterm_send_str("sleep 100 | sleep 100 | sleep 100 &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 100 | sleep 100 | sleep 100 &",
        "[1] Started",
        PROMPT
    );

    vterm_send_str("kill %1\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "kill %1",
        "[1] 'sleep' Terminated (15)",
        "    'sleep' Terminated (15)",
        "    'sleep' Terminated (15)",
        PROMPT
    );
}

TEST(shell, sigint_on_non_final_proc_prints_newline) {
    vterm_send_str("self_kill -INT | self_kill -TERM\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "self_kill -INT | self_kill -TERM",
        " ",
        "[1] 'self_kill' Interrupt (2)",
        "    'self_kill' Terminated (15)",
        PROMPT
    );
}

TEST(shell, sigint_on_final_proc) {
    vterm_send_str("self_kill -INT\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "self_kill -INT",
        " ",
        PROMPT
    );
}

TEST(shell, process_exits_normally_in_bg) {
    vterm_send_str("true &\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "true &",
        "[1] Started",
        PROMPT,
        "[1] 'true' Exited (0)",
        PROMPT
    );
}

TEST(shell, fg_process_signaled) {
    vterm_send_str("self_kill -TERM\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "self_kill -TERM",
        "[1] 'self_kill' Terminated (15)",
        PROMPT
    );
}

TEST(shell, fg_process_stopped) {
    vterm_send_str("self_kill -STOP\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "self_kill -STOP",
        "[1] Stopped",
        PROMPT
    );
}

TEST(shell, fg_process_stopped_interactively) {
    vterm_send_str("self_kill -TSTP\n");
    assert_term(3, PROMPT_LEN,
        PROMPT "self_kill -TSTP",
        " ",
        "[1] Stopped",
        PROMPT
    );
}

TEST(shell, send_sigint_to_process_that_catches_it) {
    vterm_send_str("sleep 100 | write_sleep\n");
    assert_term(2, 0,
        PROMPT "sleep 100 | write_sleep",
        "x"
    );

    vterm_send_char('c', VTERM_MOD_CTRL);
    assert_term(3, PROMPT_LEN,
        PROMPT "sleep 100 | write_sleep",
        "x",
        "^C",
        PROMPT
    );
}

TEST(shell, stopped_process_then_kill_builtin) {
    vterm_send_str("self_kill -STOP | sleep 100 &\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "self_kill -STOP | sleep 100 &",
        "[1] Started",
        PROMPT
    );

    vterm_send_str("x");
    assert_term(2, PROMPT_LEN + 1,
        PROMPT "self_kill -STOP | sleep 100 &",
        "[1] Started",
        PROMPT "x"
    );

    vterm_send_str("\b");

    if (!verify_pstate("self_kill", PSTATE_STOPPED))
        TEST_FAIL_MESSAGE("verify_pstate(\"self_kill\", PSTATE_STOPPED");

    vterm_send_str("kill %1\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "self_kill -STOP | sleep 100 &",
        "[1] Started",
        PROMPT "kill %1",
        "[1] Stopped",
        PROMPT
    );
}

TEST(shell, stop_pline_with_plines_left) {
    vterm_send_str("sleep 1 || true &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 1 || true &",
        "[1] Started",
        PROMPT
    );

    vterm_send_str("kill -STOP %1\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "kill -STOP %1",
        "[1] Stopped",
        PROMPT
    );

    vterm_send_str("kill -CONT %1\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "kill -CONT %1",
        "[1] Continued",
        PROMPT
    );

    vterm_send_str("kill %1\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "kill %1",
        "[1] 'sleep' Terminated (15)",
        PROMPT
    );
}

TEST(shell, cancel_andor_chain_with_interupt) {
    vterm_send_str("interrupt_me && echo x\n");
    assert_term(1, 1,
        PROMPT "interrupt_me && echo x",
        "x"
    );

    vterm_send_char('c', VTERM_MOD_CTRL);
    assert_term(2, PROMPT_LEN,
        PROMPT "interrupt_me && echo x",
        "x^C",
        PROMPT
    );
}

TEST(shell, cancel_andor_chain_with_signal) {
    vterm_send_str("self_kill -TERM || echo x\n");
    assert_term(3, PROMPT_LEN,
        PROMPT "self_kill -TERM || echo x",
        "[1] 'self_kill' Terminated (15)",
        "x",
        PROMPT
    );
}

// NOLINTEND(bugprone-suspicious-missing-comma)

/************ Test runner ************/

TEST_GROUP_RUNNER(shell) {
    RUN_TEST_CASE(shell, prompt_after_reclaim_tty);
    RUN_TEST_CASE(shell, prompt_after_launch_bg_cmd);
    RUN_TEST_CASE(shell, prompt_after_bg_job_done);
    RUN_TEST_CASE(shell, short_lived_bg_cmd);
    RUN_TEST_CASE(shell, interupt_foreground_job);
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
    RUN_TEST_CASE(shell, and_if_logic_bg);
    RUN_TEST_CASE(shell, or_if_logic_bg);
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
