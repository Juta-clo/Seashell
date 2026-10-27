#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "t_assert.h"
#include "t_fixture.h"
#include "unity_fixture.h"
#include "t_harness.h"

TEST_GROUP(input);

/************ Fixture ************/

TEST_SETUP(input) {
    test_setup();

    if (mkdir("/tmp/sh-tmp", 0700) == -1 && errno != EEXIST)
        xtest_fatal("mkdir");
}

TEST_TEAR_DOWN(input) {
    test_teardown();

    if (system("rm -rf /tmp/sh-tmp") != 0)
        xtest_fatal("system: %m");
}

/************ Tests ************/

// NOLINTBEGIN(bugprone-suspicious-missing-comma)

TEST(input, sigint_discards_line) {
    vt_send_str("abc");
    vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN + 2,
        PROMPT "abc"
    );

    vt_send_ch('c', VTERM_MOD_CTRL);
    assert_term(1, PROMPT_LEN,
        PROMPT "ab^C",
        PROMPT
    );
}

TEST(input, line_is_preserved_on_async_job_messages) {
    vt_send_str("sleep 100 &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] %d",
        PROMPT
    );

    vt_send_str("abc");
    vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN + 2,
        PROMPT "abc"
    );

    /* by the time the prompt is printed sh-tmp has forked. It
     * doesn't matter if it execs or not as we just want the job
     * event which will still be generated if the subshell is killed
     * before it execs */
    assert_kill(SIGTERM, "sleep");

    /* Accepted limitation for now (job_state.c:cursor_left_anchor()) */
    /* The last character is overwritten printing whitespaces to left
     * aligned the cursor and print the async message. */
    assert_term(2, PROMPT_LEN + 2,
        PROMPT "ab%s",
        "[1] Terminated       sleep 100",
        PROMPT "abc"
    );

    vt_send_str("def\n");
    assert_term(4, PROMPT_LEN,
        PROMPT "ab%s",
        "[1] Terminated       sleep 100",
        PROMPT "abdefc",
        "seashell: command not found: abdefc",
        PROMPT
    );
}

TEST(input, normal_mode_deletion_on_last_character) {
    vt_send_str("abc");
    vt_send_key(VTERM_KEY_ESCAPE, VTERM_MOD_NONE);
    vt_send_str("x");

    assert_term(0, PROMPT_LEN + 1,
        PROMPT "ab"
    );
}

TEST(input, tab_completion_with_no_line) {
    vt_send_str("\t");
    assert_term(0, PROMPT_LEN,
        PROMPT ""
    );

    vt_send_str("x");
    assert_term(0, PROMPT_LEN + 1,
        PROMPT "x"
    );

    vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN,
        PROMPT "x"
    );
}

TEST(input, tab_completion_on_filename_with_whitespace) {
    close(open("/tmp/sh-tmp/file with spaces", O_RDWR | O_CREAT, 0600));

    vt_send_str("stat /tmp/sh-tmp/file\t");
    assert_term(0, PROMPT_LEN + 35,
        PROMPT "stat /tmp/sh-tmp/file\\ with\\ spaces");
}

TEST(input, tab_completion_cycles_results) {
    close(open("/tmp/sh-tmp/file1", O_RDWR | O_CREAT, 0600));
    close(open("/tmp/sh-tmp/foo", O_RDWR | O_CREAT, 0600));
    close(open("/tmp/sh-tmp/foo bar", O_RDWR | O_CREAT, 0600));

    char *lines[3] = {
        PROMPT "stat /tmp/sh-tmp/file1",
        PROMPT "stat /tmp/sh-tmp/foo",
        PROMPT "stat /tmp/sh-tmp/foo\\ bar"
    };

    vt_send_str("stat /tmp/sh-tmp/f");

    for (size_t i = 0; i < ARR_LEN(lines) * 2; ++i) {
        char *exp = lines[i % ARR_LEN(lines)];

        vt_send_str("\t");
        assert_term(0, strlen(exp), exp);
    }
}

TEST(input, forward_slash_confirms_completion) {
    close(open("/tmp/sh-tmp/foo", O_RDWR | O_CREAT, 0600));

    vt_send_str("stat /tmp/sh-tmp/f\t");
    assert_term(0, PROMPT_LEN + 20,
        PROMPT "stat /tmp/sh-tmp/foo");

    vt_send_str("/ bar");
    assert_term(0, PROMPT_LEN + 24, PROMPT "stat /tmp/sh-tmp/foo bar");
}

TEST(input, first_word_after_pipeline_uses_path_completion) {
    vt_send_str("ls -a | gre\t");
    assert_term(0, PROMPT_LEN + 12,
        PROMPT "ls -a | grep"
    );
}

TEST(input, first_word_after_andor_uses_path_completion) {
    vt_send_str("ls -a && gre\t");
    assert_term(0, PROMPT_LEN + 13,
        PROMPT "ls -a && grep"
    );

    vt_send_str("\b\b\b\b\b\b\b|| grep\t");
    assert_term(0, PROMPT_LEN + 13,
        PROMPT "ls -a || grep"
    );
}

TEST(input, no_operation_on_history_scroll_with_non_saved) {
    vt_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    vt_send_str("foo");
    assert_term(0, PROMPT_LEN + 3,
        PROMPT "foo"
    );
}

TEST(input, history_scroll_with_one_item_saved) {
    vt_send_str("cd .\n");
    assert_term(1, PROMPT_LEN,
        PROMPT "cd .",
        PROMPT
    );

    vt_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    assert_term(1, PROMPT_LEN + 4,
        PROMPT "cd .",
        PROMPT "cd ."
    );

    vt_send_key(VTERM_KEY_DOWN, VTERM_MOD_NONE);
    assert_term(1, PROMPT_LEN,
        PROMPT "cd .",
        PROMPT
    );
}

TEST(input, scrolling_history_past_end_and_start) {
    vt_send_str("cd .\n");
    assert_term(1, PROMPT_LEN,
        PROMPT "cd .",
        PROMPT
    );

    vt_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    vt_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    assert_term(1, PROMPT_LEN + 4,
        PROMPT "cd .",
        PROMPT "cd ."
    );

    vt_send_key(VTERM_KEY_DOWN, VTERM_MOD_NONE);
    vt_send_key(VTERM_KEY_DOWN, VTERM_MOD_NONE);
    assert_term(1, PROMPT_LEN,
        PROMPT "cd .",
        PROMPT
    );
}

TEST(input, restore_typed_input_after_scrolling_history) {
    vt_send_str("cd .\n");
    assert_term(1, PROMPT_LEN,
        PROMPT "cd .",
        PROMPT
    );

    vt_send_str("foobar");
    assert_term(1, PROMPT_LEN + 6,
        PROMPT "cd .",
        PROMPT "foobar"
    );

    vt_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    assert_term(1, PROMPT_LEN + 4,
        PROMPT "cd .",
        PROMPT "cd ."
    );

    vt_send_key(VTERM_KEY_DOWN, VTERM_MOD_NONE);
    assert_term(1, PROMPT_LEN + 6,
        PROMPT "cd .",
        PROMPT "foobar"
    );
}

TEST(input, submitting_history_entry_saves_it_again) {
    vt_send_str("false\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "false",
        PROMPT
    );

    vt_send_str("true\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "true",
        PROMPT
    );

    vt_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    vt_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN + 5,
        PROMPT "false"
    );

    vt_send_str("\n");
    assert_term(1, PROMPT_LEN,
        PROMPT "false",
        PROMPT
    );

    vt_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    assert_term(1, PROMPT_LEN + 5,
        PROMPT "false",
        PROMPT "false"
    );

    vt_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    assert_term(1, PROMPT_LEN + 4,
        PROMPT "false",
        PROMPT "true"
    );

    vt_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    assert_term(1, PROMPT_LEN + 5,
        PROMPT "false",
        PROMPT "false"
    );
}

TEST(input, submitting_history_entry_with_saved_typed_input) {
    vt_send_str("false\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "false",
        PROMPT
    );

    vt_send_str("foo");
    vt_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    vt_send_str("\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "false",
        PROMPT
    );

    vt_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN + 5,
        PROMPT "false"
    );

    vt_send_key(VTERM_KEY_UP, VTERM_MOD_NONE);
    vt_send_key(VTERM_KEY_DOWN, VTERM_MOD_NONE);
    vt_send_key(VTERM_KEY_DOWN, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN,
        PROMPT
    );
}

TEST(input, history_does_not_duplicate_prompt) {
    // TODO: write this
}

TEST(input, bracketed_paste) {
    vterm_keyboard_start_paste(test->vt.vp);
    vt_send_str("PASTE\nDATA!");
    vterm_keyboard_end_paste(test->vt.vp);

    assert_term(0, PROMPT_LEN + 11,
        PROMPT "PASTE?DATA!"
    );
}

TEST(input, bracketed_paste_that_causes_emulator_scrolls) {
    test_resize_term(5, 5);

    vt_send_str("\n\n\n\n");
    assert_term(4, PROMPT_LEN,
        PROMPT,
        PROMPT,
        PROMPT,
        PROMPT,
        PROMPT
    );

    vterm_keyboard_start_paste(test->vt.vp);
    vt_send_str("XXXXXXXX");
    vterm_keyboard_end_paste(test->vt.vp);
    vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);

    assert_term(3, 4,
        PROMPT,
        PROMPT,
        PROMPT "XXX",
        "XXXXX",
        " "
    );
}

TEST(input, bracketed_paste_that_enters_pager) {
    test_resize_term(5, 5);

    vt_send_str("\n\n\n\n");
    assert_term(4, PROMPT_LEN,
        PROMPT,
        PROMPT,
        PROMPT,
        PROMPT,
        PROMPT
    );

    vterm_keyboard_start_paste(test->vt.vp);
    vt_send_str("XXXXXXXXXXXXXXXXXXXXXXX");
    vterm_keyboard_end_paste(test->vt.vp);

    assert_term(1, 0,
        ">..."
    );
}

TEST(input, large_bracketed_paste) {
    vterm_keyboard_start_paste(test->vt.vp);
    vt_send_str( /* \n only converted to 0 if paste successful */
        "\nXXXXXXXXXXXXXXXXXXXXXXXX"
        "XXXXXXXXXXXXXXXXXXXXXXXXX"
        "XXXXXXXXXXXXXXXXXXXXXXXX"
        "XXXXXXXXXXXXXXXXXXXXXXXX"
        "XXXXXXXXXXXXXXXXXXXXXX"
    );
    vterm_keyboard_end_paste(test->vt.vp);

    assert_term(2, 2,
        PROMPT "?XXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXX",
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXXXX"
               "XXXXXXXXXXXXXXXXXXXXXXXXXXXXX",
               "XX"
    );
}

TEST(input, yanked_text) {
    vt_send_str("X");
    vt_send_key(VTERM_KEY_ESCAPE, VTERM_MOD_NONE);
    vt_send_str("y");

    /* Only assert common prefix, it could be BELL or ESC \ */
    assert_bytes("\x1b]52;c;WA==");

    assert_term(0, PROMPT_LEN,
        PROMPT "X"
    );
}

TEST(input, sigchld_with_no_prompt_redraw) {
    vt_send_str("abc");
    assert_term(0, PROMPT_LEN + 3,
        PROMPT "abc"
    );

    if (kill(test->cpid, SIGCHLD) == -1)
        xtest_fatal("kill: %m");

    vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    vt_send_str("def\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "abdefc",
        "seashell: command not found: abdefc",
        PROMPT
    );
}

TEST(input, sigchld_does_not_reset_editing_mode) {
    vt_send_str("xy");
    vt_send_key(VTERM_KEY_ESCAPE, VTERM_MOD_NONE);
    vt_send_str("hx");
    assert_term(0, PROMPT_LEN,
        PROMPT "y"
    );

    if (kill(test->cpid, SIGCHLD) == -1)
        PTY_TEST_FAIL("kill: %m");

    test_pump_ms(1);

    vt_send_str("x");
    assert_term(0, PROMPT_LEN,
        PROMPT
    );
}

// NOLINTEND(bugprone-suspicious-missing-comma)

/************ Test runner ************/

TEST_GROUP_RUNNER(input) {
    RUN_TEST_CASE(input, sigint_discards_line);
    RUN_TEST_CASE(input, line_is_preserved_on_async_job_messages);
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
    RUN_TEST_CASE(input, bracketed_paste);
    RUN_TEST_CASE(input, bracketed_paste_that_causes_emulator_scrolls);
    RUN_TEST_CASE(input, bracketed_paste_that_enters_pager);
    RUN_TEST_CASE(input, large_bracketed_paste);
    RUN_TEST_CASE(input, yanked_text);
    RUN_TEST_CASE(input, sigchld_with_no_prompt_redraw);
    RUN_TEST_CASE(input, sigchld_does_not_reset_editing_mode);
}
