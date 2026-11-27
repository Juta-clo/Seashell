#include "t_assert.h"
#include "unity_fixture.h"

TEST_GROUP(config);

/************ Fixture ************/

TEST_SETUP(config) {
    test_setup("./sea", "--clean");
    assert_term(0, PROMPT_LEN, PROMPT);
}

TEST_TEAR_DOWN(config) {
    test_teardown();
}

/************ Tests ************/

// NOLINTBEGIN(bugprone-suspicious-missing-comma)

TEST(config, setting_the_prompt_function) {
    vt_send_str("sea_prompt() { echo '$ ' }\n");
    assert_term(1, 2,
        PROMPT "sea_prompt() { echo '$ ' }",
        "$ "
    );
}

TEST(config, escapes_in_prompt_are_handled) {
    vt_send_str("sea_prompt() { printf '%b ' '\\x1b[31m$\\x1b[m' }\n");
    assert_term(1, 2,
        PROMPT "sea_prompt() { printf '%%b ' '\\x1b[31m$\\x1b[m' }",
        "$ "
    );
}

TEST(config, colored_prompt_resize_below_output) {
    vt_send_str("sea_prompt() { printf '%b ' '\\x1b[31m$\\x1b[m' }\n");
    assert_term_clear(1, 2,
        PROMPT "sea_prompt() { printf '%%b ' '\\x1b[31m$\\x1b[m' }",
        "$ "
    );

    vt_send_str("echo x\n");
    assert_term(2, 2,
        "$ echo x",
        "x",
        "$ "
    );

    vt_send_str("x");
    vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    assert_term(2, 2,
        "$ echo x",
        "x",
        "$ x"
    );

    test_resize_term(15, 5);

    vt_send_str("z");
    assert_term(3, 3,
        "$ ech",
        "o x",
        "x",
        "$ zx"
    );
}

TEST(config, escape_at_rightmost_column) {
    vt_send_str("sea_prompt() { printf '%b ' 'ab\\x1b[31mcdef\\x1b[m>' }\n");
    assert_term(1, 8,
        PROMPT "sea_prompt() { printf '%%b ' 'ab\\x1b[31mcdef\\x1b[m>' }",
        "abcdef> "
    );

    test_resize_term(15, 3);

    vt_send_str("x");
    assert_term(3, 0,
        "abc",
        "def",
        "> x",
        " "
    );
}

TEST(config, prompt_with_escapes_in_pager) {
    vt_send_str("sea_prompt() { printf '%b ' '\\x1b[31mabcdef>\\x1b[m' }\n");
    assert_term_clear(1, 8,
        PROMPT "sea_prompt() { printf '%%b ' '\\x1b[31mabcdef>\\x1b[m' }",
        "abcdef> "
    );

    vt_send_str("0123456789");
    assert_term(0, 18,
        "abcdef> 0123456789"
    );

    test_resize_term(3, 3);
    assert_term(2, 0,
        ">..",
        "789",
        " "
    );
}

TEST(config, colored_prompt_text_in_pager) {
    vt_send_str("sea_prompt() { printf '%b ' '\\x1b[31mabcdefghijkl>\\x1b[m' }\n");
    assert_term_clear(1, 14,
        PROMPT "sea_prompt() { printf '%%b ' '\\x1b[31mabcdefghijkl>\\x1b[m' }",
        "abcdefghijkl> "
    );

    test_resize_term(3, 3);
    assert_term(2, 2,
        ">..",
        "jkl",
        "> "
    );
}

TEST(config, pasting_text_with_sequences) {
    vt_send_str("sea_prompt() { printf '$ ' }\n");
    assert_term_clear(1, 2,
        PROMPT "sea_prompt() { printf '$ ' }",
        "$ "
    );

    assert_term(0, 2, "$ ");

    vterm_keyboard_start_paste(test->vt.vp);
    vt_send_str("\x1b[31mX");
    vterm_keyboard_end_paste(test->vt.vp);
    assert_term(0, 8,
        "$ ?[31mX"
    );
}

TEST(config, pasting_incomplete_csi) {
    vt_send_str("sea_prompt() { printf '$ ' }\n");
    assert_term_clear(1, 2,
        PROMPT "sea_prompt() { printf '$ ' }",
        "$ "
    );

    assert_term(0, 2, "$ ");

    vterm_keyboard_start_paste(test->vt.vp);
    vt_send_str("\x1b[31");
    vterm_keyboard_end_paste(test->vt.vp);
    assert_term(0, 6,
        "$ ?[31"
    );
}

TEST(config, paste_csi_that_enters_pager) {
    vt_send_str("sea_prompt() { printf '$ ' }\n");
    assert_term_clear(1, 2,
        PROMPT "sea_prompt() { printf '$ ' }",
        "$ "
    );

    assert_term(0, 2, "$ ");

    test_resize_term(3, 3);

    vt_send_str("x");
    vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    assert_term(0, 2,
        "$ x",
        " "
    );

    vterm_keyboard_start_paste(test->vt.vp);
    vt_send_str("\x1b[0;0;0;0;0;0;0;0;0;0;0;0;0;0m");
    vterm_keyboard_end_paste(test->vt.vp);
    assert_term(2, 2,
        ">..",
        ";0;",
        "0mx"
    );

    vt_send_str("\x1b" "I");
    assert_term(0, 2,
        "$ ?",
        "[0;",
        "0;0"
    );

    vt_send_str("\x1b" "A");
    vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    assert_term(2, 2,
        ">..",
        ";0;",
        "0mx"
    );
}

TEST(config, prompt_len_zero) {
    vt_send_str("sea_prompt() { printf '%b' '\\x1b[31m\\x1b[0m' }\n");
    assert_term_clear(1, 0,
        PROMPT "sea_prompt() { printf '%%b' '\\x1b[31m\\x1b[0m' }"
    );

    test_pump_ms(3);

    vt_send_str("0123456789");
    assert_term(0, 10,
        "0123456789"
    );

    test_resize_term(3, 3);
    assert_term(1, 1,
        ">..",
        "9"
    );

    vt_send_str("\x1b" "I");
    assert_term(0, 0,
        "012",
        "345",
        "678"
    );

    vt_send_str("\x1b" "A");
    assert_term(1, 1,
        ">..",
        "9"
    );
}

TEST(config, simple_alias_expansion) {
    vt_send_str("alias p 'echo foo'\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "alias p 'echo foo'",
        PROMPT
    );

    vt_send_str("p\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "p",
        "foo",
        PROMPT
    );
}

TEST(config, alias_expands_after_pipe_from_alias) {
    vt_send_str("alias p 'echo -n abc |'; alias count 'wc -c'\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "alias p 'echo -n abc |'; alias count 'wc -c'",
        PROMPT
    );

    vt_send_str("p count\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "p count",
        "3",
        PROMPT
    );
}

TEST(config, alias_with_non_first_word) {
    vt_send_str("alias p '&& echo hi'\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "alias p '&& echo hi'",
        PROMPT
    );

    vt_send_str("p\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "p",
        "sea: parse error near '&&'",
        PROMPT
    );
}

TEST(config, alias_with_no_value) {
    vt_send_str("alias p ''\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "alias p ''",
        PROMPT
    );

    vt_send_str("p\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "p",
        "sea: parse error near 'EOF'",
        PROMPT
    );
}

TEST(config, alias_with_recursive_expansion) {
    vt_send_str("alias p 'p'\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "alias p 'p'",
        PROMPT
    );

    vt_send_str("p\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "p",
        "sea: parse error on recursive alias expansion",
        PROMPT
    );
}

TEST(config, alias_that_contains_redirect) {
    vt_send_str("alias p 'echo x >/dev/null'\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "alias p 'echo x >/dev/null'",
        PROMPT
    );

    vt_send_str("p\n");
    assert_term(1, PROMPT_LEN,
        PROMPT "p",
        PROMPT
    );
}

TEST(config, alias_that_contains_variable_expansion) {
    vt_send_str("alias p 'echo $FOO'\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "alias p 'echo $FOO'",
        PROMPT
    );

    vt_send_str("set FOO bar\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "set FOO bar",
        PROMPT
    );

    vt_send_str("p\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "p",
        "bar",
        PROMPT
    );
}

// NOLINTEND(bugprone-suspicious-missing-comma)

/************ Test runner ************/

TEST_GROUP_RUNNER(config) {
    RUN_TEST_CASE(config, setting_the_prompt_function);
    RUN_TEST_CASE(config, escapes_in_prompt_are_handled);
    RUN_TEST_CASE(config, colored_prompt_resize_below_output);
    RUN_TEST_CASE(config, escape_at_rightmost_column);
    RUN_TEST_CASE(config, prompt_with_escapes_in_pager);
    RUN_TEST_CASE(config, colored_prompt_text_in_pager);
    RUN_TEST_CASE(config, pasting_text_with_sequences);
    RUN_TEST_CASE(config, pasting_incomplete_csi);
    RUN_TEST_CASE(config, paste_csi_that_enters_pager);
    RUN_TEST_CASE(config, prompt_len_zero);
    RUN_TEST_CASE(config, simple_alias_expansion);
    RUN_TEST_CASE(config, alias_expands_after_pipe_from_alias);
    RUN_TEST_CASE(config, alias_with_non_first_word);
    RUN_TEST_CASE(config, alias_with_no_value);
    RUN_TEST_CASE(config, alias_with_recursive_expansion);
    RUN_TEST_CASE(config, alias_that_contains_redirect);
    RUN_TEST_CASE(config, alias_that_contains_variable_expansion);
}
