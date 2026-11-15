#include "t_assert.h"
#include "t_fixture.h"
#include "unity_fixture.h"
#include "t_harness.h"

TEST_GROUP(highlight);

/************ Fixture ************/

TEST_SETUP(highlight) {
    test_setup("./seashell", "--clean");
    assert_term(0, 2, PROMPT);
}

TEST_TEAR_DOWN(highlight) {
    test_teardown();
}

/************ Tests ************/

// NOLINTBEGIN(bugprone-suspicious-missing-comma)

TEST(highlight, highlight_syntax_node_beyond_viewport) {
    test_resize_term(2, 5);

    vt_send_str("echo x\n");
    assert_term_attr_clear(1, PROMPT_LEN,
        "x",
        PROMPT
    );

    vt_send_str("echo '6789'");
    assert_term_attr_clear(1, 3,
        HL_COL(c, >...),
        HL_STR("89'")
    );

    /* Requires asserting vterm cell attributes!
     * For this test 6789 should not be colored
     * as that is echo's output! */

    vt_send_ch('a', VTERM_MOD_CTRL);
    assert_term_attr_clear(0, PROMPT_LEN,
        PROMPT HL_CMD(ech),
        HL_CMD(o) " " HL_STR("'67")
    );

    vt_send_str("\n");
    assert_term_attr(1, PROMPT_LEN,
        "6789",
        PROMPT
    );
}

TEST(highlight, highlight_control_characters_dont_reset_highlighting) {
    /* For this test the second ' should be highlighted
     * like a string. */

    vt_send_str("echo '");
    vt_send_ch('y', VTERM_MOD_CTRL);
    vt_send_str("'");

    assert_term_attr_clear(0, PROMPT_LEN + 8,
        PROMPT HL_CMD(echo) " " HL_STR("'") HL_INVAL HL_STR("'")
    );
}

TEST(highlight, highlight_option) {
    vt_send_str("echo -o");
    assert_term_attr(0, PROMPT_LEN + 7,
        PROMPT HL_CMD(echo) " " HL_OPT(-o)
    );
}

TEST(highlight, highlight_long_option) {
    vt_send_str("echo --long");
    assert_term_attr(0, PROMPT_LEN + 11,
        PROMPT HL_CMD(echo) " " HL_OPT_LONG(--long)
    );
}

TEST(highlight, highlight_command_name_with_directory_prefix) {
    vt_send_str("/bin/echo");
    assert_term_attr(0, PROMPT_LEN + 9,
        PROMPT HL_CMD(/bin/echo)
    );
}

TEST(highlight, highlight_command_name_after_pipeline) {
    vt_send_str("echo | echo");
    assert_term_attr(0, PROMPT_LEN + 11,
        PROMPT HL_CMD(echo) " " HL_PIPE " " HL_CMD(echo)
    );
}

TEST(highlight, highlight_command_name_in_subshell) {
    vt_send_str("(echo)");
    assert_term_attr(0, PROMPT_LEN + 6,
        PROMPT HL_LPAREN HL_CMD(echo) HL_RPAREN
    );
}

TEST(highlight, highlight_redirect) {
    vt_send_str("echo foo >/tmp");
    assert_term_attr(0, PROMPT_LEN + 14,
        PROMPT HL_CMD(echo) " foo " HL_RDR_OUT HL_PATH(/tmp)
    );
}

TEST(highlight, highlight_redirect_with_io_number) {
    vt_send_str("echo foo 35>./bar");
    assert_term_attr(0, PROMPT_LEN + 17,
        PROMPT HL_CMD(echo) " foo " HL_IO(35) HL_RDR_OUT "./bar"
    );
}

TEST(highlight, highlight_unterminated_subshell_contents) {
    vt_send_str("(echo");
    assert_term_attr(0, PROMPT_LEN + 5,
        PROMPT HL_LPAREN HL_CMD(echo)
    );
}

TEST(highlight, highlight_subshell_after_pipeline) {
    vt_send_str("echo | (echo)");
    assert_term_attr(0, PROMPT_LEN + 13,
        PROMPT HL_CMD(echo) " " HL_PIPE " " HL_LPAREN HL_CMD(echo) HL_RPAREN
    );
}

TEST(highlight, highlight_subshell_redirect) {
    vt_send_str("(echo) > echo");
    assert_term_attr(0, PROMPT_LEN + 13,
        PROMPT HL_LPAREN HL_CMD(echo) HL_RPAREN " " HL_RDR_OUT " echo"
    );
}

TEST(highlight, highlight_command_not_found) {
    vt_send_str("non-existant");
    assert_term_attr(0, PROMPT_LEN + 12,
        PROMPT HL_NO_CMD(non-existant)
    );
}

TEST(highlight, highlight_partially_quoted_command) {
    vt_send_str("'ec'ho");
    assert_term_attr(0, PROMPT_LEN + 6,
        PROMPT HL_CMD_RAW("'ec'ho")
    );

    vt_send_ch('u', VTERM_MOD_CTRL);
    vt_send_str("'non'existant");
    assert_term_attr(0, PROMPT_LEN + 13,
        PROMPT HL_NO_CMD_RAW("'non'existant")
    );
}

TEST(highlight, highlight_multiple_quoted_segments) {
    vt_send_str("echo 'hello'beautiful\"world\"");
    assert_term_attr(0, PROMPT_LEN + 28,
        PROMPT HL_CMD(echo) " " HL_STR("'hello'")
            "beautiful" HL_STR("\"world\"")
    );
}

TEST(highlight, highlight_unterminated_quotes) {
    vt_send_str("echo 'hello");
    assert_term_attr(0, PROMPT_LEN + 11,
        PROMPT HL_CMD(echo) " " HL_STR("'hello")
    );
}

TEST(highlight, highlight_alias_in_command_position) {
    vt_send_str("alias abc foo\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "alias abc foo",
        PROMPT
    );

    vt_send_str("abc");
    assert_term_attr(0, PROMPT_LEN + 3,
        PROMPT HL_CMD(abc)
    );
}

TEST(highlight, highlight_builtin_in_command_position) {
    vt_send_str("set FOO bar");
    assert_term_attr(0, PROMPT_LEN + 11,
        PROMPT HL_CMD(set) " FOO bar"
    );
}

TEST(highlight, highlight_after_unsetting_path) {
    vt_send_str("unset PATH\n");
    assert_term_attr_clear(1, PROMPT_LEN,
        PROMPT HL_CMD(unset) " PATH",
        PROMPT
    );

    vt_send_str("echo hi");
    assert_term_attr(0, PROMPT_LEN + 7,
        PROMPT HL_NO_CMD(echo) " hi"
    );
}

TEST(highlight, highlight_async_andor_chain) {
    vt_send_str("true && false || true &");
    assert_term_attr(0, PROMPT_LEN + 23,
        PROMPT HL_CMD(true) " " HL_AND_IF " " HL_CMD(false) " " HL_OR_IF
            " " HL_CMD(true) " " HL_AMPER
    );
}

TEST(highlight, highlight_contiguous_tokens) {
    vt_send_str("true&&false>foo");
    assert_term_attr(0, PROMPT_LEN + 15,
        PROMPT HL_CMD(true) HL_AND_IF HL_CMD(false) HL_RDR_OUT "foo"
    );
}

TEST(highlight, highlight_partially_quoted_file_path) {
    vt_send_str("cat './te's");
    assert_term_attr(0, PROMPT_LEN + 11,
        PROMPT HL_CMD(cat) " " HL_STR_PATH("'./te'") HL_PATH(s)
    );
}

TEST(highlight, highlight_invalid_fd_dup_target_error) {
    vt_send_str("cat >& foo");
    assert_term_attr(0, PROMPT_LEN + 10,
        PROMPT HL_CMD(cat) " " HL_FD_OUT " " HL_SYNTAX(foo)
    );
}

TEST(highlight, highlight_pipe_error) {
    vt_send_str("|");
    assert_term_attr(0, PROMPT_LEN + 1,
        PROMPT HL_SYNTAX(|)
    );
}

TEST(highlight, highlight_pipe_after_pipe_error) {
    vt_send_str("cat ||");
    assert_term_attr(0, PROMPT_LEN + 6,
        PROMPT HL_CMD(cat) " " HL_SYNTAX(||)
    );
}

TEST(highlight, highlight_andor_error) {
    vt_send_str("&&");
    assert_term_attr(0, PROMPT_LEN + 2,
        PROMPT HL_SYNTAX(&&)
    );
}

TEST(highlight, highlight_error_in_subshell) {
    vt_send_str("(true && & echo");
    assert_term_attr(0, PROMPT_LEN + 15,
        PROMPT HL_LPAREN HL_CMD(true) " " HL_AND_IF " " HL_SYNTAX(&)
          " " HL_CMD(echo)
    );
}

IGNORE_TEST(highlight, higlight_subshell_errors_are_contained) {
    vt_send_str("(&&) > out");
    assert_term_attr(0, PROMPT_LEN + 10,
        PROMPT HL_LPAREN HL_AND_IF HL_RPAREN " " HL_RDR_OUT " out"
    );
}

TEST(highlight, highlight_error_after_initial_error) {
    vt_send_str("| echo");
    assert_term_attr(0, PROMPT_LEN + 6,
        PROMPT HL_SYNTAX(|) " " HL_CMD(echo)
    );
}

// NOLINTEND(bugprone-suspicious-missing-comma)

/************ Test runner ************/

TEST_GROUP_RUNNER(highlight) {
    RUN_TEST_CASE(highlight, highlight_syntax_node_beyond_viewport);
    RUN_TEST_CASE(highlight, highlight_control_characters_dont_reset_highlighting);
    RUN_TEST_CASE(highlight, highlight_option);
    RUN_TEST_CASE(highlight, highlight_long_option);
    RUN_TEST_CASE(highlight, highlight_command_name_with_directory_prefix);
    RUN_TEST_CASE(highlight, highlight_command_name_after_pipeline);
    RUN_TEST_CASE(highlight, highlight_command_name_in_subshell);
    RUN_TEST_CASE(highlight, highlight_redirect);
    RUN_TEST_CASE(highlight, highlight_redirect_with_io_number);
    RUN_TEST_CASE(highlight, highlight_unterminated_subshell_contents);
    RUN_TEST_CASE(highlight, highlight_subshell_after_pipeline);
    RUN_TEST_CASE(highlight, highlight_subshell_redirect);
    RUN_TEST_CASE(highlight, highlight_command_not_found);
    RUN_TEST_CASE(highlight, highlight_partially_quoted_command);
    RUN_TEST_CASE(highlight, highlight_multiple_quoted_segments);
    RUN_TEST_CASE(highlight, highlight_unterminated_quotes);
    RUN_TEST_CASE(highlight, highlight_alias_in_command_position);
    RUN_TEST_CASE(highlight, highlight_builtin_in_command_position);
    RUN_TEST_CASE(highlight, highlight_after_unsetting_path);
    RUN_TEST_CASE(highlight, highlight_async_andor_chain);
    RUN_TEST_CASE(highlight, highlight_contiguous_tokens);
    RUN_TEST_CASE(highlight, highlight_partially_quoted_file_path);
    RUN_TEST_CASE(highlight, highlight_invalid_fd_dup_target_error);
    RUN_TEST_CASE(highlight, highlight_pipe_error);
    RUN_TEST_CASE(highlight, highlight_andor_error);
    RUN_TEST_CASE(highlight, highlight_error_in_subshell);
    RUN_TEST_CASE(highlight, higlight_subshell_errors_are_contained);
    RUN_TEST_CASE(highlight, highlight_error_after_initial_error);
}
