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
    test_setup("./seashell", "--clean");
    assert_term(0, 2, PROMPT);

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

    test_pump_ms(3);

    vt_send_str("x");
    assert_term(0, PROMPT_LEN,
        PROMPT
    );
}

/* Completion */

TEST(input, complete_with_no_line) {
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

TEST(input, complete_on_filename_with_whitespace) {
    close(open("/tmp/sh-tmp/file with spaces", O_RDWR | O_CREAT, 0600));

    vt_send_str("stat /tmp/sh-tmp/file\t");
    assert_term(0, PROMPT_LEN + 35,
        PROMPT "stat /tmp/sh-tmp/file\\ with\\ spaces");
}

TEST(input, complete_cycles_results) {
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

TEST(input, complete_forward_slash_confirms_completion) {
    close(open("/tmp/sh-tmp/foo", O_RDWR | O_CREAT, 0600));

    vt_send_str("stat /tmp/sh-tmp/f\t");
    assert_term(0, PROMPT_LEN + 20,
        PROMPT "stat /tmp/sh-tmp/foo");

    vt_send_str("/ bar");
    assert_term(0, PROMPT_LEN + 24, PROMPT "stat /tmp/sh-tmp/foo bar");
}

TEST(input, complete_first_word_after_pipeline_completes_path) {
    vt_send_str("ls -a | gre\t");
    assert_term(0, PROMPT_LEN + 12,
        PROMPT "ls -a | grep"
    );
}

TEST(input, complete_first_word_after_andor_uses_path_completion) {
    vt_send_str("ls -a && gre\t");
    assert_term(0, PROMPT_LEN + 13,
        PROMPT "ls -a && grep"
    );

    vt_send_str("\b\b\b\b\b\b\b|| grep\t");
    assert_term(0, PROMPT_LEN + 13,
        PROMPT "ls -a || grep"
    );
}

TEST(input, complete_command_before_pipe) {
    vt_send_str("gre |");
    vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN + 3,
        PROMPT "gre |"
    );

    vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN + 4,
        PROMPT "grep |"
    );
}

TEST(input, complete_file_target_redirect_after_subshell) {
    vt_send_str("(echo hi) > /tmp/sh-tm");
    vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN + 24,
        PROMPT "(echo hi) > /tmp/sh-tmp/"
    );
}

TEST(input, complete_glob_in_completion_glob) {
    vt_send_str("stat /tmp/sh*tm");
    vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN + 17,
        PROMPT "stat /tmp/sh-tmp/"
    );
}

TEST(input, complete_on_redirect_with_no_target) {
    vt_send_str("echo >");
    vt_send_ch('a', VTERM_MOD_CTRL);
    vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN,
        PROMPT "echo >"
    );
}

TEST(input, complete_command_in_subshell) {
    vt_send_str("(gre)");
    vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN + 5,
        PROMPT "(grep)"
    );
}

TEST(input, complete_command_in_incomplete_subshell) {
    vt_send_str("(gre");
    vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN + 5,
        PROMPT "(grep"
    );
}

TEST(input, complete_command_in_incomplete_subshell_before_pipe) {
    vt_send_str("(gre | a");
    for (size_t i = 0; i < 4; ++i)
        vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN + 5,
        PROMPT "(grep | a"
    );
}

TEST(input, complete_command_in_subshell_after_pipe) {
    vt_send_str("a | (gre)");
    vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN + 9,
        PROMPT "a | (grep)"
    );
}

TEST(input, complete_on_forward_slash) {
    vt_send_str("mkdir /tmp/sh-tmp/foo\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "mkdir /tmp/sh-tmp/foo",
        PROMPT
    );

    vt_send_str("touch /tmp/sh-tmp/a\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "touch /tmp/sh-tmp/a",
        PROMPT
    );

    vt_send_str("echo >/dev/null /tmp/sh-tmp/");
    vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
    assert_term_clear(0, PROMPT_LEN + 29,
        PROMPT "echo >/dev/null /tmp/sh-tmp/a"
    );
}

TEST(input, complete_on_relative_path) {
    vt_send_str("cd ./asse");
    vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
    assert_term_clear(0, PROMPT_LEN + 12,
        PROMPT "cd ./assets/"
    );
}

TEST(input, complete_relative_path_no_relative_specifier) {
    vt_send_str("cd asse");
    vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
    assert_term_clear(0, PROMPT_LEN + 10,
        PROMPT "cd assets/"
    );
}

TEST(input, complete_relative_path_with_prefix) {
    vt_send_str("cd ./src/main.");
    vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
    assert_term_clear(0, PROMPT_LEN + 15,
        PROMPT "cd ./src/main.c"
    );
}

TEST(input, complete_prefix_with_spaces_that_are_already_escaped) {
    vt_send_str("mkdir '/tmp/sh-tmp/a b'\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "mkdir '/tmp/sh-tmp/a b'",
        PROMPT
    );

    vt_send_str("touch '/tmp/sh-tmp/a b/c'\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "touch '/tmp/sh-tmp/a b/c'",
        PROMPT
    );

    vt_send_str("cd /tmp/sh-tmp/a\\ b/");
    vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
    assert_term_clear(0, PROMPT_LEN + 21,
        PROMPT "cd /tmp/sh-tmp/a\\ b/c"
    );
}

TEST(input, complete_wildcard_directory_cycles_concrete_paths) {
    TEST_ASSERT_EQUAL_INT(0, mkdir("/tmp/sh-tmp/foo", 0700));
    TEST_ASSERT_EQUAL_INT(0, mkdir("/tmp/sh-tmp/food", 0700));

    const char *files[] = {
        "/tmp/sh-tmp/foo/bar",
        "/tmp/sh-tmp/food/bar",
    };

    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); ++i) {
        int fd = open(files[i], O_WRONLY | O_CREAT | O_TRUNC, 0600);
        TEST_ASSERT_TRUE(fd >= 0);
        TEST_ASSERT_EQUAL_INT(0, close(fd));
    }

    char *expected[] = {
        PROMPT "stat /tmp/sh-tmp/foo/bar",
        PROMPT "stat /tmp/sh-tmp/food/bar",
        PROMPT "stat /tmp/sh-tmp/foo/bar",
    };

    vt_send_str("stat /tmp/sh-tmp/f*/ba");

    /* Keep the same completion session active: first match, second, wrap. */
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
        assert_term(0, strlen(expected[i]), expected[i]);
    }
}

TEST(input, syntax_node_beyond_viewport) {
    test_resize_term(2, 5);

    vt_send_str("echo x\n");
    assert_term_attr_clear(1, PROMPT_LEN,
        "x",
        PROMPT
    );

    vt_send_str("echo '6789'");
    assert_term_attr_clear(1, 3,
        "%[c]>...%[]",
        "%[G]89'%[]"
    );

    /* Requires asserting vterm cell attributes!
     * For this test 6789 should not be colored
     * as that is echo's output! */

    vt_send_ch('a', VTERM_MOD_CTRL);
    assert_term_attr_clear(0, PROMPT_LEN,
        PROMPT "%[g]ech",
        "o%[] %[G]'67%[]"
    );

    vt_send_str("\n");
    assert_term_attr(1, PROMPT_LEN,
        "6789",
        PROMPT
    );
}

TEST(input, control_characters_dont_reset_highlighting) {
    /* For this test the second ' should be highlighted
     * like a string. */

    vt_send_str("echo '");
    vt_send_ch('y', VTERM_MOD_CTRL);
    vt_send_str("'");

    assert_term_attr_clear(0, PROMPT_LEN + 8,
        PROMPT "%[g]echo%[] %[G]'%[]%[b,r,b]?%[]%[G]'%[]"
    );
}

TEST(input, highlight_option) {
    vt_send_str("echo -o");
    assert_term_attr(0, PROMPT_LEN + 7,
        PROMPT "%[g]echo%[] %[c]-o%[]"
    );
}

TEST(input, highlight_long_option) {
    vt_send_str("echo --long");
    assert_term_attr(0, PROMPT_LEN + 11,
        PROMPT "%[g]echo%[] %[x]--long%[]"
    );
}

TEST(input, highlight_command_name_with_directory_prefix) {
    vt_send_str("/bin/echo");
    assert_term_attr(0, PROMPT_LEN + 9,
        PROMPT "%[g]/bin/echo%[]"
    );
}

TEST(input, highlight_command_name_after_pipeline) {
    vt_send_str("echo | echo");
    assert_term_attr(0, PROMPT_LEN + 11,
        PROMPT "%[g]echo%[] %[y]|%[] %[g]echo%[]"
    );
}

TEST(input, highlight_command_name_in_subshell) {
    vt_send_str("(echo)");
    assert_term_attr(0, PROMPT_LEN + 6,
        PROMPT "%[y](%[g]echo%[y])%[]"
    );
}

TEST(input, highlight_redirect) {
    vt_send_str("echo foo >/tmp");
    assert_term_attr(0, PROMPT_LEN + 14,
        PROMPT "%[g]echo%[] foo %[c]>%[]%[,,u]/tmp%[]"
    );
}

TEST(input, highlight_redirect_with_io_number) {
    vt_send_str("echo foo 35>./bar");
    assert_term_attr(0, PROMPT_LEN + 17,
        PROMPT "%[g]echo%[] foo %[c]35>%[]./bar"
    );
}

TEST(input, highlight_unterminated_subshell_contents) {
    vt_send_str("(echo");
    assert_term_attr(0, PROMPT_LEN + 5,
        PROMPT "%[y](%[g]echo%[]"
    );
}

TEST(input, highlight_subshell_after_pipeline) {
    vt_send_str("echo | (echo)");
    assert_term_attr(0, PROMPT_LEN + 13,
        PROMPT "%[g]echo%[] %[y]|%[] %[y](%[g]echo%[y])%[]"
    );
}

TEST(input, highlight_subshell_redirect) {
    vt_send_str("(echo) > echo");
    assert_term_attr(0, PROMPT_LEN + 13,
        PROMPT "%[y](%[g]echo%[y])%[] %[c]>%[] echo"
    );
}

TEST(input, highlight_command_not_found) {
    vt_send_str("non-existant");
    assert_term_attr(0, PROMPT_LEN + 12,
        PROMPT "%[r,,b]non-existant%[]"
    );
}

TEST(input, highlight_partially_quoted_command) {
    vt_send_str("'ec'ho");
    assert_term_attr(0, PROMPT_LEN + 6,
        PROMPT "%[G]'ec'%[g]ho%[]"
    );

    vt_send_ch('u', VTERM_MOD_CTRL);
    vt_send_str("'non'existant");
    assert_term_attr(0, PROMPT_LEN + 13,
        PROMPT "%[r,,b]'non'existant%[]"
    );
}

TEST(input, highlight_multiple_quoted_segments) {
    vt_send_str("echo 'hello'beautiful\"world\"");
    assert_term_attr(0, PROMPT_LEN + 28,
        PROMPT "%[g]echo%[] %[G]'hello'%[]beautiful%[G]\"world\"%[]"
    );
}

TEST(input, highlight_unterminated_quotes) {
    vt_send_str("echo 'hello");
    assert_term_attr(0, PROMPT_LEN + 11,
        PROMPT "%[g]echo%[] %[G]'hello%[]"
    );
}

TEST(input, highlight_alias_in_command_position) {
    vt_send_str("alias abc foo\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "alias abc foo",
        PROMPT
    );

    vt_send_str("abc");
    assert_term_attr(0, PROMPT_LEN + 3,
        PROMPT "%[g]abc%[]"
    );
}

TEST(input, highlight_builtin_in_command_position) {
    vt_send_str("set FOO bar");
    assert_term_attr(0, PROMPT_LEN + 11,
        PROMPT "%[g]set%[] FOO bar"
    );
}

TEST(input, highlight_after_unsetting_path) {
    vt_send_str("unset PATH\n");
    assert_term_attr_clear(1, PROMPT_LEN,
        PROMPT "%[g]unset%[] PATH",
        PROMPT
    );

    vt_send_str("echo hi");
    assert_term_attr(0, PROMPT_LEN + 7,
        PROMPT "%[r,,b]echo%[] hi"
    );
}

TEST(input, highlight_async_andor_chain) {
    vt_send_str("true && false || true &");
    assert_term_attr(0, PROMPT_LEN + 23,
        PROMPT "%[g]true%[] %[y]&&%[] %[g]false%[] %[y]||%[] %[g]true%[] "
            "%[c,,b]&%[]"
    );
}

TEST(input, highlight_contiguous_tokens) {
    vt_send_str("true&&false>foo");
    assert_term_attr(0, PROMPT_LEN + 15,
        PROMPT "%[g]true%[y]&&%[g]false%[c]>%[]foo"
    );
}

TEST(input, highlight_partially_quoted_file_path) {
    vt_send_str("cat './te's");
    assert_term_attr(0, PROMPT_LEN + 11,
        PROMPT "%[g]cat%[] %[G,,u]'./te'%[]%[,,u]s"
    );
}

TEST(input, trailing_slash_on_unreadable_directory) {
    vt_send_str("mkdir -p -m 0300 /tmp/unreadable\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "mkdir -p -m 0300 /tmp/unreadable",
        PROMPT
    );

    vt_send_str("cd /tmp/unreadable/");
    assert_term_attr(0, PROMPT_LEN + 19,
        PROMPT "%[g]cd%[] /tmp/unreadable/"
    );
}

// NOLINTEND(bugprone-suspicious-missing-comma)

/************ Test runner ************/

TEST_GROUP_RUNNER(input) {
    RUN_TEST_CASE(input, sigint_discards_line);
    RUN_TEST_CASE(input, line_is_preserved_on_async_job_messages);
    RUN_TEST_CASE(input, normal_mode_deletion_on_last_character);
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

    /* Completion */
    RUN_TEST_CASE(input, complete_with_no_line);
    RUN_TEST_CASE(input, complete_on_filename_with_whitespace);
    RUN_TEST_CASE(input, complete_cycles_results);
    RUN_TEST_CASE(input, complete_forward_slash_confirms_completion);
    RUN_TEST_CASE(input, complete_first_word_after_pipeline_completes_path);
    RUN_TEST_CASE(input, complete_first_word_after_andor_uses_path_completion);
    RUN_TEST_CASE(input, complete_command_before_pipe);
    RUN_TEST_CASE(input, complete_file_target_redirect_after_subshell);
    RUN_TEST_CASE(input, complete_glob_in_completion_glob);
    RUN_TEST_CASE(input, complete_on_redirect_with_no_target);
    RUN_TEST_CASE(input, complete_command_in_subshell);
    RUN_TEST_CASE(input, complete_command_in_incomplete_subshell);
    RUN_TEST_CASE(input, complete_command_in_subshell_after_pipe);
    RUN_TEST_CASE(input, complete_on_forward_slash);
    RUN_TEST_CASE(input, complete_on_relative_path);
    RUN_TEST_CASE(input, complete_relative_path_no_relative_specifier);
    RUN_TEST_CASE(input, complete_relative_path_with_prefix);
    RUN_TEST_CASE(input, complete_prefix_with_spaces_that_are_already_escaped);
    RUN_TEST_CASE(input, complete_wildcard_directory_cycles_concrete_paths);

    /* Highlighting */
    RUN_TEST_CASE(input, syntax_node_beyond_viewport);
    RUN_TEST_CASE(input, control_characters_dont_reset_highlighting);
    RUN_TEST_CASE(input, highlight_option);
    RUN_TEST_CASE(input, highlight_long_option);
    RUN_TEST_CASE(input, highlight_command_name_with_directory_prefix);
    RUN_TEST_CASE(input, highlight_command_name_after_pipeline);
    RUN_TEST_CASE(input, highlight_command_name_in_subshell);
    RUN_TEST_CASE(input, highlight_redirect);
    RUN_TEST_CASE(input, highlight_redirect_with_io_number);
    RUN_TEST_CASE(input, highlight_unterminated_subshell_contents);
    RUN_TEST_CASE(input, highlight_subshell_after_pipeline);
    RUN_TEST_CASE(input, highlight_subshell_redirect);
    RUN_TEST_CASE(input, highlight_command_not_found);
    RUN_TEST_CASE(input, highlight_partially_quoted_command);
    RUN_TEST_CASE(input, highlight_multiple_quoted_segments);
    RUN_TEST_CASE(input, highlight_unterminated_quotes);
    RUN_TEST_CASE(input, highlight_alias_in_command_position);
    RUN_TEST_CASE(input, highlight_builtin_in_command_position);
    RUN_TEST_CASE(input, highlight_after_unsetting_path);
    RUN_TEST_CASE(input, highlight_async_andor_chain);
    RUN_TEST_CASE(input, highlight_contiguous_tokens);
    RUN_TEST_CASE(input, highlight_partially_quoted_file_path);

    RUN_TEST_CASE(input, trailing_slash_on_unreadable_directory);
}
