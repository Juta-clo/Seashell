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

TEST(input, tab_completion_on_filename_with_whitespace) {
    xclose(xopen("file with spaces", O_RDWR | O_CREAT | O_EXCL, 0600));

    vterm_send_str("stat file\t");
    bool rv = term_ensure(LN(
        PROMPT "stat file\\ with\\ spaces"
    ), 0, PROMPT_LEN + 23);
    take_vt_snapshot();

    xunlink("file with spaces");

    if (!rv)
       TEST_FAIL_MESSAGE("term_ensure_failed");
}

TEST(input, tab_completion_cycles_results) {
    xclose(xopen("file1", O_RDWR | O_CREAT | O_EXCL, 0600));
    xclose(xopen("foo", O_RDWR | O_CREAT | O_EXCL, 0600));
    xclose(xopen("foo bar", O_RDWR | O_CREAT | O_EXCL, 0600));

    char *lines[3] = {
        PROMPT "stat file1",
        PROMPT "stat foo",
        PROMPT "stat foo\\ bar"
    };

    vterm_send_str("stat f");

    bool rv;
    for (size_t i = 0; i < ARR_LEN(lines) * 2; ++i) {
        char *exp = lines[i % ARR_LEN(lines)];

        vterm_send_str("\t");
        if (!(rv = term_ensure(LN(exp), 0, strlen(exp))))
            break;
        take_vt_snapshot();
    }

    xunlink("file1");
    xunlink("foo");
    xunlink("foo bar");

    if (!rv)
       TEST_FAIL_MESSAGE("term_ensure_failed");
}

TEST(input, forward_slash_confirms_completion) {
    xclose(xopen("foo", O_RDWR | O_CREAT | O_EXCL, 0600));

    bool rv;

    vterm_send_str("stat f\t");
    if (!(rv = term_ensure(LN(PROMPT "stat foo"), 0, PROMPT_LEN + 8)))
        goto cleanup;
    take_vt_snapshot();

    vterm_send_str("/ bar");
    if (!(rv = term_ensure(LN(PROMPT "stat foo bar"), 0, PROMPT_LEN + 12)))
        goto cleanup;
    take_vt_snapshot();

cleanup:
    xunlink("foo");

    if (!rv)
       TEST_FAIL_MESSAGE("term_ensure_failed");
}

TEST(input, first_word_after_pipeline_uses_path_completion) {
    vterm_send_str("ls -a | gre\t");
    assert_term(0, PROMPT_LEN + 12,
        PROMPT "ls -a | grep"
    );
}

TEST(input, first_word_after_andor_uses_path_completion) {
    vterm_send_str("ls -a && gre\t");
    assert_term(0, PROMPT_LEN + 13,
        PROMPT "ls -a && grep"
    );

    vterm_send_str("\b\b\b\b\b\b\b|| grep\t");
    assert_term(0, PROMPT_LEN + 13,
        PROMPT "ls -a || grep"
    );
}

TEST(input, no_operation_on_history_scroll_with_non_saved) {
    vterm_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    vterm_send_str("foo");
    assert_term(0, PROMPT_LEN + 3,
        PROMPT "foo"
    );
}

TEST(input, history_scroll_with_one_item_saved) {
    vterm_send_str("cd .\n");
    assert_term(1, PROMPT_LEN,
        PROMPT "cd .",
        PROMPT
    );

    vterm_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    assert_term(1, PROMPT_LEN + 4,
        PROMPT "cd .",
        PROMPT "cd ."
    );

    vterm_send_key(VTERM_KEY_DOWN, VTERM_MOD_NONE);
    assert_term(1, PROMPT_LEN,
        PROMPT "cd .",
        PROMPT
    );
}

TEST(input, scrolling_history_past_end_and_start) {
    vterm_send_str("cd .\n");
    assert_term(1, PROMPT_LEN,
        PROMPT "cd .",
        PROMPT
    );

    vterm_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    vterm_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    assert_term(1, PROMPT_LEN + 4,
        PROMPT "cd .",
        PROMPT "cd ."
    );

    vterm_send_key(VTERM_KEY_DOWN, VTERM_MOD_NONE);
    vterm_send_key(VTERM_KEY_DOWN, VTERM_MOD_NONE);
    assert_term(1, PROMPT_LEN,
        PROMPT "cd .",
        PROMPT
    );
}

TEST(input, restore_typed_input_after_scrolling_history) {
    vterm_send_str("cd .\n");
    assert_term(1, PROMPT_LEN,
        PROMPT "cd .",
        PROMPT
    );

    vterm_send_str("foobar");
    assert_term(1, PROMPT_LEN + 6,
        PROMPT "cd .",
        PROMPT "foobar"
    );

    vterm_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    assert_term(1, PROMPT_LEN + 4,
        PROMPT "cd .",
        PROMPT "cd ."
    );

    vterm_send_key(VTERM_KEY_DOWN, VTERM_MOD_NONE);
    assert_term(1, PROMPT_LEN + 6,
        PROMPT "cd .",
        PROMPT "foobar"
    );
}

TEST(input, submitting_history_entry_saves_it_again) {
    vterm_send_str("false\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "false",
        PROMPT
    );

    vterm_send_str("true\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "true",
        PROMPT
    );

    vterm_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    vterm_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN + 5,
        PROMPT "false"
    );

    vterm_send_str("\n");
    assert_term(1, PROMPT_LEN,
        PROMPT "false",
        PROMPT
    );

    vterm_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    assert_term(1, PROMPT_LEN + 5,
        PROMPT "false",
        PROMPT "false"
    );

    vterm_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    assert_term(1, PROMPT_LEN + 4,
        PROMPT "false",
        PROMPT "true"
    );

    vterm_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    assert_term(1, PROMPT_LEN + 5,
        PROMPT "false",
        PROMPT "false"
    );
}

TEST(input, submitting_history_entry_with_saved_typed_input) {
    vterm_send_str("false\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "false",
        PROMPT
    );

    vterm_send_str("foo");
    vterm_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    vterm_send_str("\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "false",
        PROMPT
    );

    vterm_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN + 5,
        PROMPT "false"
    );

    vterm_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    vterm_send_key(VTERM_KEY_DOWN, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN,
        PROMPT
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
    RUN_TEST_CASE(input, tab_completion_on_filename_with_whitespace);
    RUN_TEST_CASE(input, tab_completion_cycles_results);
    RUN_TEST_CASE(input, forward_slash_confirms_completion);
    RUN_TEST_CASE(input, first_word_after_pipeline_uses_path_completion);
    RUN_TEST_CASE(input, first_word_after_andor_uses_path_completion);
    RUN_TEST_CASE(input, no_operation_on_history_scroll_with_non_saved);
    RUN_TEST_CASE(input, history_scroll_with_one_item_saved);
    RUN_TEST_CASE(input, scrolling_history_past_end_and_start);
    RUN_TEST_CASE(input, restore_typed_input_after_scrolling_history);
    RUN_TEST_CASE(input, submitting_history_entry_saves_it_again);
    RUN_TEST_CASE(input, submitting_history_entry_with_saved_typed_input);
}
