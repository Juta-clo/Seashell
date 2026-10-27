#include <assert.h>
#include <sys/timerfd.h>
#include <sys/poll.h>
#include <unistd.h>
#include <pty.h>
#include <sys/wait.h>

#include "t_harness.h" // IWYU pragma: keep
#include "t_fixture.h"
#include "unity_fixture.h"
#include "t_assert.h"

TEST_GROUP(shell);

/************ Fixture ************/

TEST_SETUP(shell) {
    test_setup("./seashell", "--clean");
}

TEST_TEAR_DOWN(shell) {
    test_teardown();
}

/************ Tests ************/

/* Must ensure that the program is in the foreground group
 * before writing to the pty, else the bytes will be interpreted
 * by seashells termios settings. */

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

    assert_kill(SIGTERM, "sleep");

    assert_term(2, PROMPT_LEN + 1,
        PROMPT "x%s",
        "[1] Terminated       sleep 100",
        PROMPT "x"
    );
}

TEST(shell, short_lived_bg_cmd) {
    vt_send_str("true &\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "true &",
        "[1] %d",
        PROMPT "%s",
        "[1] done             true",
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
        "[1] Stopped          write_sleep%s",
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

    assert_kill(SIGTERM, "sleep");

    assert_term(3, 0,
        PROMPT "write_sleep",
        "x",
        "[1] Terminated       sleep 100%s"
    );
}

TEST(shell, builtin_in_bg) {
    vt_send_str("cd . &\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "cd . &",
        "[1] %d",
        PROMPT "%s",
        "[1] done             cd .",
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

TEST(shell, simple_andor) {
    vt_send_str("true && /bin/echo x\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "true && /bin/echo x",
        "x",
        PROMPT
    );
}

TEST(shell, simple_bg_andor) {
    vt_send_str("true && /bin/echo x &\n");
    assert_term(5, PROMPT_LEN,
        PROMPT "true && /bin/echo x &",
        "[1] %d",
        PROMPT "x",
        "[1] done             true &&%s",
        "    done             /bin/echo x",
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
    assert_term_clear(5, PROMPT_LEN,
        PROMPT "true && /bin/echo x &",
        "[1] %d",
        PROMPT "x",
        "[1] done             true &&%s",
        "    done             /bin/echo x",
        PROMPT
    );

    vt_send_str("false && /bin/echo x &\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "false && /bin/echo x &",
        "[1] %d",
        PROMPT "%s",
        "[1] exit 1           false",
        PROMPT
    );
}

TEST(shell, or_if_logic_bg) {
    vt_send_str("false || /bin/echo y &\n");
    assert_term_clear(5, PROMPT_LEN,
        PROMPT "false || /bin/echo y &",
        "[1] %d",
        PROMPT "y",
        "[1] exit 1           false ||%s",
        "    done             /bin/echo y",
        PROMPT
    );

    vt_send_str("true || /bin/echo y &\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "true || /bin/echo y &",
        "[1] %d",
        PROMPT "%s",
        "[1] done             true",
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
    assert_term(2, PROMPT_LEN,
        PROMPT "jobs",
        "[1] running          sleep 100%s",
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
        PROMPT "%s",
        "[1] Stopped (tty input) cat",
        PROMPT
    );

    vt_send_str("fg %1\n");
    assert_term(2, 0,
        PROMPT "fg %%1",
        "[1] running          cat%s"
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
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "jobs",
        "[1] running          sleep 100%s",
        PROMPT
    );

    vt_send_str("kill %1\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "kill %%1",
        "[1] Terminated       sleep 100%s",
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
        "[1] Stopped (signal) sleep 100%s",
        PROMPT
    );

    vt_send_str("bg %1\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "bg %%1",
        "[1] running          sleep 100%s",
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
        "[1] Terminated       sleep 100 | sleep 100 | sleep 100%s",
        PROMPT
    );
}

TEST(shell, sigint_on_non_final_proc_prints_newline) {
    vt_send_str("self_kill -INT | self_kill -TERM\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "self_kill -INT | self_kill -TERM",
        "",
        "[1] Interrupt        self_kill -INT |%s",
        "    Terminated       self_kill -TERM",
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
        PROMPT "%s",
        "[1] done             true",
        PROMPT
    );
}

TEST(shell, fg_process_signaled) {
    vt_send_str("self_kill -TERM\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "self_kill -TERM",
        "[1] Terminated       self_kill -TERM%s",
        PROMPT
    );
}

TEST(shell, fg_process_stopped) {
    vt_send_str("self_kill -STOP\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "self_kill -STOP",
        "[1] Stopped (signal) self_kill -STOP%s",
        PROMPT
    );
}

TEST(shell, fg_process_stopped_interactively) {
    vt_send_str("self_kill -TSTP\n");
    assert_term(3, PROMPT_LEN,
        PROMPT "self_kill -TSTP",
        "",
        "[1] Stopped          self_kill -TSTP%s",
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
        "[1] Stopped (signal) self_kill -STOP |%s",
        "    Terminated       sleep 100",
        PROMPT
    );
}

TEST(shell, stop_pline_with_plines_left) {
    vt_send_str("sleep 100 || true &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 100 || true &",
        "[1] %d",
        PROMPT
    );

    vt_send_str("kill -STOP %1\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "kill -STOP %%1",
        "[1] Stopped (signal) sleep 100%s",
        PROMPT
    );

    vt_send_str("kill -CONT %1\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "kill -CONT %%1",
        "[1] running          sleep 100%s",
        PROMPT
    );

    vt_send_str("kill %1\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "kill %%1",
        PROMPT "%s",
        "[1] Terminated       sleep 100 ||",
        "    done             true",
        PROMPT
    );
}

TEST(shell, cancel_andor_with_interupt) {
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

TEST(shell, cancel_andor_with_signal) {
    vt_send_str("self_kill -TERM || echo x\n");
    assert_term(3, PROMPT_LEN,
        PROMPT "self_kill -TERM || echo x",
        "[1] Terminated       self_kill -TERM%s",
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

    assert_kill(SIGTERM, "sleep");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "%s",
        "[1] Terminated       sleep 100",
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

TEST(shell, set_builtin_affects_plines_in_andor) {
    vt_send_str("set FOO x && echo $FOO && set FOO y && echo $FOO\n");
    assert_term(3, PROMPT_LEN,
        PROMPT "set FOO x && echo $FOO && set FOO y && echo $FOO",
        "x",
        "y",
        PROMPT
    );
}

TEST(shell, async_andor_message) {
    vt_send_str("self_kill -TERM || true && self_kill -HUP &\n");
    assert_term(6, PROMPT_LEN,
        PROMPT "self_kill -TERM || true && self_kill -HUP &",
        "[1] %d",
        PROMPT "%s",
        "[1] Terminated       self_kill -TERM ||",
        "    done             true &&",
        "    Hangup           self_kill -HUP",
        PROMPT
    );
}

TEST(shell, async_andor_stop_message_all_plines_execed) {
    vt_send_str("true && sleep 100 &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "true && sleep 100 &",
        "[1] %d",
        PROMPT
    );

    assert_pstate("sleep", PSTATE_SLEEPING);

    vt_send_str("kill -TSTP %1\n");
    assert_term_clear(3, PROMPT_LEN,
        PROMPT "kill -TSTP %%1",
        "[1] done             true &&%s",
        "    Stopped          sleep 100",
        PROMPT
    );

    vt_send_str("kill -CONT %1\n");
    assert_term_clear(3, PROMPT_LEN,
        PROMPT "kill -CONT %%1",
        "[1] done             true &&%s",
        "    running          sleep 100",
        PROMPT
    );

    assert_pstate("sleep", PSTATE_SLEEPING);

    vt_send_str("kill %1\n");
    assert_term_clear(3, PROMPT_LEN,
        PROMPT "kill %%1",
        "[1] done             true &&%s",
        "    Terminated       sleep 100",
        PROMPT
    );
}

TEST(shell, async_andor_stop_message_unexeced_plines) {
    vt_send_str("true && self_kill -TSTP && sleep 100 &\n");
    assert_term_clear(5, PROMPT_LEN,
        PROMPT "true && self_kill -TSTP && sleep 100 &",
        "[1] %d",
        PROMPT "%s",
        "[1] done             true &&",
        "    Stopped          self_kill -TSTP",
        PROMPT
    );

    vt_send_str("kill -CONT %1\n");
    assert_term_clear(3, PROMPT_LEN,
        PROMPT "kill -CONT %%1",
        "[1] done             true &&%s",
        "    running          self_kill -TSTP",
        PROMPT
    );

    assert_pstate("sleep", PSTATE_SLEEPING);

    vt_send_str("kill %1\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "kill %%1",
        "[1] done             true &&%s",
        "    done             self_kill -TSTP &&",
        "    Terminated       sleep 100",
        PROMPT
    );
}

TEST(shell, async_andor_with_builtin) {
    vt_send_str("true && cd . && true &\n");
    assert_term(6, PROMPT_LEN,
        PROMPT "true && cd . && true &",
        "[1] %d",
        PROMPT "%s",
        "[1] done             true &&",
        "    done             cd . &&",
        "    done             true",
        PROMPT
    );
}

TEST(shell, async_andor_with_multi_cmd_pline) {
    vt_send_str("self_kill -TERM|self_kill -HUP||true|true|false&\n");
    assert_term(7, PROMPT_LEN,
        PROMPT "self_kill -TERM|self_kill -HUP||true|true|false&",
        "[1] %d",
        PROMPT "%s",
        "[1] Terminated       self_kill -TERM |",
        "    Hangup           self_kill -HUP ||",
        "    done             true | true |",
        "    exit 1           false",
        PROMPT
    );
}

TEST(shell, async_andor_message_after_skipped_commands) {
    vt_send_str("true || false && true &\n");
    assert_term(5, PROMPT_LEN,
        PROMPT "true || false && true &",
        "[1] %d",
        PROMPT "%s",
        "[1] done             true &&",
        "    done             true",
        PROMPT
    );
}

TEST(shell, async_andor_with_parent_shell_builtin) {
    vt_send_str("cd . && self_kill -TSTP\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "cd . && self_kill -TSTP",
        "",
        "[1] done             cd . &&%s",
        "    Stopped          self_kill -TSTP",
        PROMPT
    );
}

TEST(shell, fg_resumed_job_stopped_again) {
    vt_send_str("sleep 100 &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] %d",
        PROMPT
    );

    vt_send_str("kill -STOP %1\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "kill -STOP %%1",
        "[1] Stopped (signal) sleep 100%s",
        PROMPT
    );

    vt_send_str("fg %1 || echo x\n");
    assert_term(2, 0,
        PROMPT "fg %%1 || echo x",
        "[1] running          sleep 100%s"
    );

    verify_pstate("sleep", PSTATE_SLEEPING);

    vt_send_ch('z', VTERM_MOD_CTRL);
    assert_term_clear(5, PROMPT_LEN,
        PROMPT "fg %%1 || echo x",
        "[1] running          sleep 100%s",
        "^Z",
        "[1] Stopped          sleep 100%s",
        "x",
        PROMPT
    );

    vt_send_str("kill -TERM %1\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "kill -TERM %%1",
        PROMPT
    );

    vt_send_str("fg %1\n");
    assert_term(3, PROMPT_LEN,
        PROMPT "fg %%1",
        "[1] running          sleep 100%s",
        "[1] Terminated       sleep 100%s",
        PROMPT
    );
}

TEST(shell, fg_builtin_part_of_job_plan) {
    vt_send_str("sleep 100 &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] %d",
        PROMPT
    );

    vt_send_str("kill -STOP %1\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "kill -STOP %%1",
        "[1] Stopped (signal) sleep 100%s",
        PROMPT
    );

    vt_send_str("echo x && fg %1 || echo x\n");
    assert_term(3, 0,
        PROMPT "echo x && fg %%1 || echo x",
        "x",
        "[1] running          sleep 100%s"
    );

    verify_pstate("sleep", PSTATE_SLEEPING);

    vt_send_ch('c', VTERM_MOD_CTRL);
    assert_term(5, PROMPT_LEN,
        PROMPT "echo x && fg %%1 || echo x",
        "x",
        "[1] running          sleep 100%s",
        "^C",
        "x",
        PROMPT
    );
}

TEST(shell, fg_bg_kill_regression) {
    vt_send_str("sleep 100 &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] %d",
        PROMPT
    );

    vt_send_str("kill -STOP %1\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "kill -STOP %%1",
        "[1] Stopped (signal) sleep 100%s",
        PROMPT
    );

    vt_send_str("fg %1\n");
    assert_term(2, 0,
        PROMPT "fg %%1",
        "[1] running          sleep 100%s"
    );

    assert_pstate("sleep", PSTATE_SLEEPING);

    vt_send_ch('c', VTERM_MOD_CTRL);
    assert_term_clear(3, PROMPT_LEN,
        PROMPT "fg %%1",
        "[1] running          sleep 100%s",
        "^C",
        PROMPT
    );

    vt_send_str("sleep 100 &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] %d",
        PROMPT
    );

    vt_send_str("true && kill %1\n");
    assert_term_clear(3, PROMPT_LEN,
        PROMPT "true && kill %%1",
        PROMPT "%s",
        "[1] Terminated       sleep 100",
        PROMPT
    );
}

TEST(shell, multiple_fg_builtins_in_andor) {
    vt_send_str("sleep 100 &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] %d",
        PROMPT
    );

    vt_send_str("kill -STOP %1\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "kill -STOP %%1",
        "[1] Stopped (signal) sleep 100%s",
        PROMPT
    );

    vt_send_str("cat &\n");
    assert_term_clear(4, PROMPT_LEN,
        PROMPT "cat &",
        "[2] %d",
        PROMPT "%s",
        "[2] Stopped (tty input) cat",
        PROMPT
    );

    vt_send_str("fg %1 || fg %2 || echo x\n");
    assert_term(2, 0,
        PROMPT "fg %%1 || fg %%2 || echo x",
        "[1] running          sleep 100%s"
    );

    assert_kill(SIGTERM, "sleep");
    assert_pstate("cat", PSTATE_SLEEPING);

    vt_send_ch('c', VTERM_MOD_CTRL);
    assert_term(6, PROMPT_LEN,
        PROMPT "fg %%1 || fg %%2 || echo x",
        "[1] running          sleep 100%s",
        "[1] Terminated       sleep 100%s",
        "[2] running          cat%s",
        "^C",
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
    RUN_TEST_CASE(shell, simple_andor);
    RUN_TEST_CASE(shell, simple_bg_andor);
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
    RUN_TEST_CASE(shell, cancel_andor_with_interupt);
    RUN_TEST_CASE(shell, cancel_andor_with_signal);
    RUN_TEST_CASE(shell, job_id_creation_after_unordered);
    RUN_TEST_CASE(shell, set_builtin_affects_plines_in_andor);
    RUN_TEST_CASE(shell, async_andor_message);
    RUN_TEST_CASE(shell, async_andor_stop_message_all_plines_execed);
    RUN_TEST_CASE(shell, async_andor_stop_message_unexeced_plines);
    RUN_TEST_CASE(shell, async_andor_with_builtin);
    RUN_TEST_CASE(shell, async_andor_with_multi_cmd_pline);
    RUN_TEST_CASE(shell, async_andor_message_after_skipped_commands);
    RUN_TEST_CASE(shell, async_andor_with_parent_shell_builtin);
    RUN_TEST_CASE(shell, fg_resumed_job_stopped_again);
    RUN_TEST_CASE(shell, fg_builtin_part_of_job_plan);
    RUN_TEST_CASE(shell, fg_bg_kill_regression);
    RUN_TEST_CASE(shell, multiple_fg_builtins_in_andor);
}
