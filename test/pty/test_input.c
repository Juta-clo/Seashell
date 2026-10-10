#include "unity_fixture.h"
#include "pty_test.h"

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
    assert_term(0, PROMPT_LEN + 2,
        PROMPT "abc"
    );

    vterm_send_char('c', VTERM_MOD_CTRL);
    assert_term(1, PROMPT_LEN,
        PROMPT "ab^C",
        PROMPT
    );
}

TEST(input, line_is_preserved_on_async_job_messages) {
    vterm_send_str("sleep 100 &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] Started",
        PROMPT
    );

    vterm_send_str("abc");
    vterm_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN + 2,
        PROMPT "abc"
    );

    /* by the time the prompt is printed seashell has forked. It
     * doesn't matter if it execs or not as we just want the job
     * event which will still be generated if the subshell is killed
     * before it execs */
    kill_child("sleep");

    assert_term(2, PROMPT_LEN + 2,
        PROMPT "abc",
        "[1] 'sleep' Terminated (15)",
        PROMPT "abc"
    );

    vterm_send_str("def\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "abc",
        "[1] 'sleep' Terminated (15)",
        PROMPT "abdefc ",
        "seashell: command not found: abdefc",
        PROMPT
    );
}

TEST(input, sigchld_with_no_prompt_redraw) {
    vterm_send_str("abc");
    assert_term(0, PROMPT_LEN + 3,
        PROMPT "abc"
    );

    xkill(test->cpid, SIGCHLD);

    vterm_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    vterm_send_str("def\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "abdefc",
        "seashell: command not found: abdefc",
        PROMPT
    );
}

TEST(input, normal_mode_deletion_on_last_character) {
    vterm_send_str("abc");
    vterm_send_key(VTERM_KEY_ESCAPE, VTERM_MOD_NONE);
    vterm_send_str("x");

    assert_term(0, PROMPT_LEN + 1,
        PROMPT "ab "
    );
}

TEST(input, tab_completion_with_no_line) {
    vterm_send_str("\t");
    assert_term(0, PROMPT_LEN,
        PROMPT ""
    );

    vterm_send_str("x");
    assert_term(0, PROMPT_LEN + 1,
        PROMPT "x"
    );

    vterm_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN,
        PROMPT "x"
    );
}

// NOLINTEND(bugprone-suspicious-missing-comma)

/************ Test runner ************/

TEST_GROUP_RUNNER(input) {
    RUN_TEST_CASE(input, sigint_discards_line);
    RUN_TEST_CASE(input, line_is_preserved_on_async_job_messages);
    RUN_TEST_CASE(input, sigchld_with_no_prompt_redraw);
    RUN_TEST_CASE(input, normal_mode_deletion_on_last_character);
    RUN_TEST_CASE(input, tab_completion_with_no_line);
}
