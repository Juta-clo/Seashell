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
    test_setup();
}

TEST_TEAR_DOWN(shell) {
    test_teardown();
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
    vt_send_str("/bin/echo x\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "/bin/echo x",
        "x",
        PROMPT
    );
}

TEST(shell, prompt_after_launch_bg_cmd) {
    vt_send_str("sleep 100 &\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] %d",
        PROMPT
    );
}

TEST(shell, prompt_after_bg_job_done) {
    vt_send_str("sleep 100 &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] %d",
        PROMPT
    );

    vt_send_str("x");
    assert_term(0, PROMPT_LEN + 1,
        PROMPT "x"
    );

    TEST_ASSERT_MESSAGE(verify_kill("sleep"), "kill_child(\"sleep\")");

    assert_term(2, PROMPT_LEN + 1,
        PROMPT "x",
        "[1] Terminated      sleep 100",
        PROMPT "x"
    );
}

TEST(shell, short_lived_bg_cmd) {
    vt_send_str("true &\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "true &",
        "[1] %d",
        PROMPT,
        "[1] done      true",
        PROMPT
    );
}

TEST(shell, interupt_foreground_job) {
    vt_send_str("write_sleep\n");
    assert_term(2, 0,
        PROMPT "write_sleep",
        "x"
    );

    vt_send_ch('c', VTERM_MOD_CTRL);
    assert_term(3, PROMPT_LEN,
        PROMPT "write_sleep",
        "x",
        "^C%%",
        PROMPT
    );
}

TEST(shell, stop_fg_job) {
    vt_send_str("write_sleep\n");
    assert_term(2, 0,
        PROMPT "write_sleep",
        "x"
    );

    vt_send_ch('z', VTERM_MOD_CTRL);
    assert_term(4, PROMPT_LEN,
        PROMPT "write_sleep",
        "x",
        "^Z",
        "[1] Stopped      write_sleep",
        PROMPT
    );
}

TEST(shell, bg_jobs_pid_state) {
    vt_send_str("sleep 100 &\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] %d",
        PROMPT
    );

    vt_send_str("sleep 100 &\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] %d",
        PROMPT "sleep 100 &",
        "[2] %d",
        PROMPT
    );
}

TEST(shell, unknown_cmd) {
    vt_send_str("zoobar\n"); /* hopefully no-one ever makes this... */
    assert_term(2, PROMPT_LEN,
        PROMPT "zoobar",
        "seashell: command not found: zoobar",
        PROMPT
    );
}

TEST(shell, pipeline) {
    /* wc outputs 3 because it includes the newline from grep */
    vt_send_str("/bin/echo hi | grep h | wc -c\n");
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

    vt_send_str("sleep 100 &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] %d",
        PROMPT
    );

    vt_send_str("write_sleep\n"); /* lose fgroup control */
    assert_term(2, 0,
        PROMPT "write_sleep",
        "x"
    );

    TEST_ASSERT_MESSAGE(verify_kill("sleep"), "kill_child(\"sleep\")");

    assert_term(3, 0,
        PROMPT "write_sleep",
        "x",
        "[1] Terminated      sleep 100"
    );
}

TEST(shell, builtin_in_bg) {
    vt_send_str("cd . &\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "cd . &",
        "[1] %d",
        PROMPT,
        "[1] done      cd .",
        PROMPT
    );
}

TEST(shell, pipeline_of_builtins_does_not_duplicate_prompt) {

    /* Regression: after adding display_prompt() to try_run_builtin()
     * and neglecting to confirm if we are in the fg, and not calling
     * from a subshell, the prompt was duplicated. */

    vt_send_str("jobs | cd .\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "jobs | cd .",
        "jobs: no job control in this shell",
        PROMPT
    );
}

TEST(shell, simple_andor_chain) {
    vt_send_str("true && /bin/echo x\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "true && /bin/echo x",
        "x",
        PROMPT
    );
}

TEST(shell, simple_bg_andor_chain) {
    vt_send_str("true && /bin/echo x &\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "true && /bin/echo x &",
        "[1] %d",
        PROMPT "x",
        "[1] done      /bin/echo x",
        PROMPT
    );
}

TEST(shell, and_if_logic) {
    vt_send_str("true && /bin/echo x\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "true && /bin/echo x",
        "x",
        PROMPT
    );

    vt_send_str("false && /bin/echo x\n");
    assert_term(1, PROMPT_LEN,
        PROMPT "false && /bin/echo x",
        PROMPT
    );
}

TEST(shell, or_if_logic) {
    vt_send_str("false || /bin/echo y\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "false || /bin/echo y",
        "y",
        PROMPT
    );

    vt_send_str("true || /bin/echo y\n");
    assert_term(1, PROMPT_LEN,
        PROMPT "true || /bin/echo y",
        PROMPT
    );
}

TEST(shell, and_if_logic_bg) {
    vt_send_str("true && /bin/echo x &\n");
    assert_term_clear(4, PROMPT_LEN,
        PROMPT "true && /bin/echo x &",
        "[1] %d",
        PROMPT "x",
        "[1] done      /bin/echo x",
        PROMPT
    );

    vt_send_str("false && /bin/echo x &\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "false && /bin/echo x &",
        "[1] %d",
        PROMPT,
        "[1] done      false",
        PROMPT
    );
}

TEST(shell, or_if_logic_bg) {
    vt_send_str("false || /bin/echo y &\n");
    assert_term_clear(4, PROMPT_LEN,
        PROMPT "false || /bin/echo y &",
        "[1] %d",
        PROMPT "y",
        "[1] done      /bin/echo y",
        PROMPT
    );

    vt_send_str("true || /bin/echo y &\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "true || /bin/echo y &",
        "[1] %d",
        PROMPT,
        "[1] done      true",
        PROMPT
    );
}

TEST(shell, and_or_chain_with_builtins) {
    vt_send_str("echo x && echo y\n");
    assert_term(3, PROMPT_LEN,
        PROMPT "echo x && echo y",
        "x",
        "y",
        PROMPT
    );
}

TEST(shell, prompt_after_launch_fg_builtin) {
    vt_send_str("cd .\n");
    assert_term(1, PROMPT_LEN,
        PROMPT "cd .",
        PROMPT
    );
}

TEST(shell, jobs_builtin) {
    vt_send_str("jobs\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "jobs",
        PROMPT
    );

    vt_send_str("sleep 100 &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] %d",
        PROMPT
    );

    vt_send_str("jobs\n");
    assert_term(3, PROMPT_LEN,
        PROMPT "jobs",
        "[1] Running",
        "   sleep 100 Running",
        PROMPT
    );
}

TEST(shell, builtin_and_or_logic) {
    vt_send_str("cd /non-existant || echo ?\n");
    assert_term_clear(3, PROMPT_LEN,
        PROMPT "cd /non-existant || echo ?",
        "cd: chdir: No such file or directory",
        "?",
        PROMPT
    );

    vt_send_str("cd /non-existant && echo ?\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "cd /non-existant && echo ?",
        "cd: chdir: No such file or directory",
        PROMPT
    );
}

TEST(shell, fg_stopped_bg_job) {
    vt_send_str("cat &\n");
    assert_term_clear(4, PROMPT_LEN,
        PROMPT "cat &",
        "[1] %d",
        PROMPT,
        "[1] Stopped (tty input)      cat",
        PROMPT
    );

    vt_send_str("fg %1\n");
    assert_term(2, 0,
        PROMPT "fg %%1",
        "[1] continued      cat"
    );
}

TEST(shell, kill_builtin_works_on_bg_jobs) {
    vt_send_str("sleep 100 &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] %d",
        PROMPT
    );

    vt_send_str("jobs\n");
    assert_term_clear(3, PROMPT_LEN,
        PROMPT "jobs",
        "[1] Running",
        "   sleep 100 Running",
        PROMPT
    );

    vt_send_str("kill %1\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "kill %%1",
        "[1] Terminated      sleep 100",
        PROMPT
    );

    vt_send_str("jobs\n");
    assert_term(1, PROMPT_LEN,
        PROMPT "jobs",
        PROMPT
    );
}

TEST(shell, bg_on_stopped_job_resumes_it) {
    vt_send_str("sleep 100 &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] %d",
        PROMPT
    );

    vt_send_str("kill -STOP %1\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "kill -STOP %%1",
        "[1] Stopped (signal)      sleep 100",
        PROMPT
    );

    vt_send_str("bg %1\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "bg %%1",
        "[1] continued      sleep 100",
        PROMPT
    );
}

TEST(shell, kill_multiple_proc_job) {
    vt_send_str("sleep 100 | sleep 100 | sleep 100 &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 100 | sleep 100 | sleep 100 &",
        "[1] %d",
        PROMPT
    );

    vt_send_str("kill %1\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "kill %%1",
        "[1] Terminated      sleep 100 | sleep 100 | sleep 100",
        PROMPT
    );
}

TEST(shell, sigint_on_non_final_proc_prints_newline) {
    vt_send_str("self_kill -INT | self_kill -TERM\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "self_kill -INT | self_kill -TERM",
        "",
        "[1] Interrupt      self_kill -INT |",
        "    Terminated     self_kill -TERM",
        PROMPT
    );
}

TEST(shell, sigint_on_final_proc) {
    vt_send_str("self_kill -INT\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "self_kill -INT",
        "",
        PROMPT
    );
}

TEST(shell, process_exits_normally_in_bg) {
    vt_send_str("true &\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "true &",
        "[1] %d",
        PROMPT,
        "[1] done      true",
        PROMPT
    );
}

TEST(shell, fg_process_signaled) {
    vt_send_str("self_kill -TERM\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "self_kill -TERM",
        "[1] Terminated      self_kill -TERM",
        PROMPT
    );
}

TEST(shell, fg_process_stopped) {
    vt_send_str("self_kill -STOP\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "self_kill -STOP",
        "[1] Stopped (signal)      self_kill -STOP",
        PROMPT
    );
}

TEST(shell, fg_process_stopped_interactively) {
    vt_send_str("self_kill -TSTP\n");
    assert_term(3, PROMPT_LEN,
        PROMPT "self_kill -TSTP",
        "",
        "[1] Stopped      self_kill -TSTP",
        PROMPT
    );
}

TEST(shell, send_sigint_to_process_that_catches_it) {
    vt_send_str("sleep 100 | write_sleep\n");
    assert_term(2, 0,
        PROMPT "sleep 100 | write_sleep",
        "x"
    );

    vt_send_ch('c', VTERM_MOD_CTRL);
    assert_term(3, PROMPT_LEN,
        PROMPT "sleep 100 | write_sleep",
        "x",
        "^C",
        PROMPT
    );
}

TEST(shell, stopped_process_then_kill_builtin) {
    vt_send_str("self_kill -STOP | sleep 100 &\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "self_kill -STOP | sleep 100 &",
        "[1] %d",
        PROMPT
    );

    vt_send_str("x");
    assert_term(2, PROMPT_LEN + 1,
        PROMPT "self_kill -STOP | sleep 100 &",
        "[1] %d",
        PROMPT "x"
    );

    vt_send_str("\b");
    assert_pstate("self_kill", PSTATE_STOPPED);

    vt_send_str("kill %1\n");
    assert_term(5, PROMPT_LEN,
        PROMPT "self_kill -STOP | sleep 100 &",
        "[1] %d",
        PROMPT "kill %%1",
        "[1] Stopped (signal)      self_kill -STOP |",
        "    Terminated            sleep 100",
        PROMPT
    );
}

TEST(shell, stop_pline_with_plines_left) {
    vt_send_str("sleep 1 || true &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 1 || true &",
        "[1] %d",
        PROMPT
    );

    vt_send_str("kill -STOP %1\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "kill -STOP %%1",
        "[1] Stopped (signal)      sleep 1",
        PROMPT
    );

    vt_send_str("kill -CONT %1\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "kill -CONT %%1",
        "[1] continued      sleep 1",
        PROMPT
    );

    vt_send_str("kill %1\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "kill %%1",
        "[1] Terminated      sleep 1",
        PROMPT
    );
}

TEST(shell, cancel_andor_chain_with_interupt) {
    vt_send_str("interrupt_me && echo x\n");
    assert_term(1, 1,
        PROMPT "interrupt_me && echo x",
        "x"
    );

    vt_send_ch('c', VTERM_MOD_CTRL);
    assert_term(2, PROMPT_LEN,
        PROMPT "interrupt_me && echo x",
        "x^C",
        PROMPT
    );
}

TEST(shell, cancel_andor_chain_with_signal) {
    vt_send_str("self_kill -TERM || echo x\n");
    assert_term(3, PROMPT_LEN,
        PROMPT "self_kill -TERM || echo x",
        "[1] Terminated      self_kill -TERM",
        "x",
        PROMPT
    );
}

TEST(shell, job_id_creation_after_unordered) {
    vt_send_str("sleep 100 &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] %d",
        PROMPT
    );

    vt_send_str("yes >/dev/null &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "yes >/dev/null &",
        "[2] %d",
        PROMPT
    );

    verify_kill("sleep");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT,
        "[1] Terminated      sleep 100",
        PROMPT
    );

    vt_send_str("sleep 100 &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] %d",
        PROMPT
    );

    vt_send_str("sleep 100 &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[3] %d",
        PROMPT
    );
}

TEST(shell, set_builtin_affects_plines_in_andor_chain) {
    vt_send_str("set FOO x && echo $FOO && set FOO y && echo $FOO\n");
    assert_term_clear(3, PROMPT_LEN,
        PROMPT "set FOO x && echo $FOO && set FOO y && echo $FOO",
        "x",
        "y",
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
    RUN_TEST_CASE(shell, job_id_creation_after_unordered);
    RUN_TEST_CASE(shell, set_builtin_affects_plines_in_andor_chain);
}
