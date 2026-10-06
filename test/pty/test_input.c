#include "unity_fixture.h"
#include "pty_test.h"
#include "proc_view.h"

TEST_GROUP(input);

/************ Fixture ************/

TEST_SETUP(input) {
    new_shell();
}

TEST_TEAR_DOWN(input) {
    delete_shell();
}

/************ Tests ************/

// NOLINTBEGIN(bugprone-suspicious-missing-comma)

TEST(input, sigint_discards_line) {
    vterm_send_str("abc");
    vterm_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    assert_cursor(0, PROMPT_LEN + 2);
    assert_term(
        PROMPT "abc"
    );

    vterm_send_char('c', VTERM_MOD_CTRL);
    assert_cursor(1, PROMPT_LEN);
    assert_term(
        PROMPT "ab^C",
        PROMPT
    );
}

TEST(input, line_is_preserved_on_async_job_messages) {
    vterm_send_str("sleep 100 &\n");
    assert_cursor(2, PROMPT_LEN);
    assert_term(
        PROMPT "sleep 100 &",
        "[1] Started",
        PROMPT
    );

    vterm_send_str("abc");
    vterm_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    assert_cursor(2, PROMPT_LEN + 2);
    assert_term(
        PROMPT "sleep 100 &",
        "[1] Started",
        PROMPT "abc"
    );

    /* by the time the prompt is printed seashell has forked. It
     * doesn't matter if it execs or not as we just want the job
     * event which will still be generated if the subshell is killed
     * before it execs */
    da_pstat pstats;
    TEST_ASSERT_NOT_EQUAL_INT(-1, child_pstat(cur_test->cpid, &pstats));
    TEST_ASSERT_EQUAL_size_t(1, pstats.size);
    xkill(pstats.data[0].pid, SIGTERM);
    da_free(&pstats);

    assert_cursor(4, PROMPT_LEN + 2);
    assert_term(
        PROMPT "sleep 100 &",
        "[1] Started",
        PROMPT "abc",
        "[1] 'sleep' Terminated (15)",
        PROMPT "abc"
    );

    vterm_send_str("def\n");
    assert_cursor(6, PROMPT_LEN);
    assert_term(
        PROMPT "sleep 100 &",
        "[1] Started",
        PROMPT "abc",
        "[1] 'sleep' Terminated (15)",
        PROMPT "abdefc ",
        "seashell: command not found: abdefc",
        PROMPT
    );
}

TEST(input, sigchld_with_no_prompt_redraw) {
    vterm_send_str("abc");
    assert_term(
        PROMPT "abc"
    );

    xkill(cur_test->cpid, SIGCHLD);

    vterm_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    vterm_send_str("def\n");
    assert_cursor(2, PROMPT_LEN);
    assert_term(
        PROMPT "abdefc",
        "seashell: command not found: abdefc",
        PROMPT
    );
}

TEST(input, normal_mode_deletion_on_last_character) {
    vterm_send_str("abc");
    vterm_send_key(VTERM_KEY_ESCAPE, VTERM_MOD_NONE);
    vterm_send_str("x");

    assert_cursor(0, PROMPT_LEN + 1);
    assert_term(PROMPT "ab ");
}

// NOLINTEND(bugprone-suspicious-missing-comma)

/************ Test runner ************/

TEST_GROUP_RUNNER(input) {
    RUN_TEST_CASE(input, sigint_discards_line);
    RUN_TEST_CASE(input, line_is_preserved_on_async_job_messages);
    RUN_TEST_CASE(input, sigchld_with_no_prompt_redraw);
    RUN_TEST_CASE(input, normal_mode_deletion_on_last_character);
}
