#include <stdio.h>

#include "t_assert.h"
#include "unity_fixture.h"

TEST_GROUP(config);

/************ Fixture ************/

FILE *config;

__attribute__ ((__format__(printf, 1, 2)))
void create_config(const char *fmt, ...) {
    config = fopen("/tmp/config_test.lua", "w+");
    if (!config)
        xtest_fatal("fopen: %m");

    va_list va;
    va_start(va, fmt);

    if (vfprintf(config, fmt, va) < 0)
        xtest_fatal("fprintf: %m");

    va_end(va);

    if (fclose(config) == EOF)
        xtest_fatal("fclose: %m");
}

void set_prompt(const char *prompt) {
    create_config(
        "return {\n"
            "prompt = function()\n"
                "return \"%s\"\n"
            "end\n"
        "}\n", prompt
    );

    test_setup("./seashell", "--config-file=/tmp/config_test.lua");
}

TEST_SETUP(config) {
    config = nullptr;
}

TEST_TEAR_DOWN(config) {
    test_teardown();
}

/************ Tests ************/

// NOLINTBEGIN(bugprone-suspicious-missing-comma)

TEST(config, option_config_file_read) {
    set_prompt("$ ");
    assert_term(0, 2,
        "$ "
    );
}

TEST(config, escapes_in_prompt_are_handled) {
    set_prompt("\\27[31m$\\27[m ");
    assert_term(0, 2,
        "$ "
    );

    vt_send_str("echo x\n");
    assert_term(2, 2,
        "$ echo x",
        "x",
        "$ "
    );
}

TEST(config, colored_prompt_resize_below_output) {
    set_prompt("\\27[31m$\\27[m ");
    assert_term(0, 2,
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
    set_prompt("ab\\27[31mcdef\\27[m> ");
    assert_term(0, 8,
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
    set_prompt("\\27[31mabcdef> \\27[m");
    assert_term(0, 8,
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
    set_prompt("\\27[31mabcdefghijkl> \\27[m");
    assert_term(0, 14,
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
    set_prompt("$ ");
    assert_term(0, 2,
        "$ "
    );

    vterm_keyboard_start_paste(test->vt.vp);
    vt_send_str("\x1b[31mX");
    vterm_keyboard_end_paste(test->vt.vp);
    assert_term(0, 8,
        "$ ?[31mX"
    );
}

TEST(config, pasting_incomplete_csi) {
    set_prompt("$ ");
    assert_term(0, 2,
        "$ "
    );

    vterm_keyboard_start_paste(test->vt.vp);
    vt_send_str("\x1b[31");
    vterm_keyboard_end_paste(test->vt.vp);
    assert_term(0, 6,
        "$ ?[31"
    );
}

TEST(config, paste_csi_that_enters_pager) {
    set_prompt("$ ");
    assert_term(0, 2,
         "$ "
    );

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
    set_prompt("\\27[31m\\27[0m");
    test_pump_ms(3);
    assert_term(0, 0,
        ""
    );

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

IGNORE_TEST(config, history_does_not_duplicate_colored_prompt) {
    // Requires, hot reloading of config file to change prompt
}

// NOLINTEND(bugprone-suspicious-missing-comma)

/************ Test runner ************/

TEST_GROUP_RUNNER(config) {
    RUN_TEST_CASE(config, option_config_file_read);
    RUN_TEST_CASE(config, escapes_in_prompt_are_handled);
    RUN_TEST_CASE(config, colored_prompt_resize_below_output);
    RUN_TEST_CASE(config, escape_at_rightmost_column);
    RUN_TEST_CASE(config, prompt_with_escapes_in_pager);
    RUN_TEST_CASE(config, colored_prompt_text_in_pager);
    RUN_TEST_CASE(config, pasting_text_with_sequences);
    RUN_TEST_CASE(config, pasting_incomplete_csi);
    RUN_TEST_CASE(config, paste_csi_that_enters_pager);
    RUN_TEST_CASE(config, prompt_len_zero);
    RUN_TEST_CASE(config, history_does_not_duplicate_colored_prompt);
}
