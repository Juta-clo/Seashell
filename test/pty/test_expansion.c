#define _GNU_SOURCE

#include "unity_fixture.h"
#include "pty_test.h"

TEST_GROUP(expansion);

/************ Fixture ************/

TEST_SETUP(expansion) {
    new_shell();

    pty_send("set FOO bar\n");
    pty_ensure(PROMPT);
}

TEST_TEAR_DOWN(expansion) {
    delete_shell();
}

/************ Tests ************/

TEST(expansion, memtest) {
    /* setting and unsetting */
    for (int i = 0; i < 100; ++i) {
        pty_send("set FOO bar\n");
        pty_ensure(PROMPT);

        pty_send("unset FOO\n");
        pty_ensure(PROMPT);
    }

    /* continuously restting */
    for (int i = 0; i < 100; ++i) {
        pty_send("set FOO bar\n");
        pty_ensure(PROMPT);
    }
    pty_send("unset FOO\n");
    pty_ensure(PROMPT);

    /* setting a large amount of variables */
    for (int i = 0; i < 100; ++i) {
        char cmd_buf[4096];
        snprintf(cmd_buf, 4096, "set VAR%d x\n", i + 1);
        pty_send(cmd_buf);
        pty_ensure(PROMPT);
    }
    for (int i = 0; i < 100; ++i) {
        char cmd_buf[4096];
        snprintf(cmd_buf, 4096, "unset VAR%d\n", i + 1);
        pty_send(cmd_buf);
        pty_ensure(PROMPT);
    }
}

TEST(expansion, simple_expansion) {
    pty_send("echo $FOO\n");
    pty_ensure("bar\n");
    pty_ensure(PROMPT);
}

TEST(expansion, back_to_back_variables) {
    pty_send("echo $FOO$FOO\n");
    pty_ensure("barbar\n");
    pty_ensure(PROMPT);
}

TEST(expansion, regular_text_then_variable) {
    pty_send("echo abc$FOO\n");
    pty_ensure("abcbar\n");
    pty_ensure(PROMPT);
}

TEST(expansion, variable_then_regular_text) {
    pty_send("echo $FOOabc\n");
    pty_ensure("\n");
    pty_ensure(PROMPT);
}

TEST(expansion, variable_then_escaped_regular_text) {
    pty_send("echo $FOO\\abc\n");
    pty_ensure("barabc\n");
    pty_ensure(PROMPT);
}

TEST(expansion, variable_then_regular_delim) {
    pty_send("echo $FOO.txt\n");
    pty_ensure("bar.txt\n");
    pty_ensure(PROMPT);

    pty_send("echo $FOO/home\n");
    pty_ensure("bar/home\n");
    pty_ensure(PROMPT);

    pty_send("/bin/echo $FOO bee\n");
    pty_ensure("bar bee\n");
    pty_ensure(PROMPT);
}

TEST(expansion, escaped_variable_does_not_expand) {
    pty_send("echo \\$FOO\n");
    pty_ensure("$FOO\n");
    pty_ensure(PROMPT);
}

TEST(expansion, escaped_variable_after_variable) {
    pty_send("echo $FOO\\$FOO");
    pty_ensure("\n");
    pty_ensure(PROMPT);
}

TEST(expansion, set_builtin_fails_on_bad_key) {
    pty_send("set \\$FOO bar\n");
    pty_ensure("set: failed to add variable\n");
    pty_ensure(PROMPT);
}

TEST(expansion, variable_with_space_expands) {
    pty_send("set FOO \"b a r\"\n");
    pty_ensure(PROMPT);

    pty_send("echo $FOO\n");
    pty_ensure("b a r\n");
    pty_ensure(PROMPT);
}

TEST(expansion, variable_with_quoted_and_unquoted_segments) {
    pty_send("set FOO hello\" beautiful\"' world!'\n");
    pty_ensure(PROMPT);

    pty_send("echo $FOO\n");
    pty_ensure("hello beautiful world!\n");
    pty_ensure(PROMPT);
}

TEST(expansion, variable_then_escaped_variable) {
    pty_send("echo $FOO\\$FOO\n");
    pty_ensure("bar$FOO\n");
    pty_ensure(PROMPT);
}

/************ Test runner ************/

TEST_GROUP_RUNNER(expansion) {
    /* Run with -m -g expansion -n memtest */
    RUN_TEST_CASE(expansion, memtest);

    RUN_TEST_CASE(expansion, simple_expansion);
    RUN_TEST_CASE(expansion, back_to_back_variables);
    RUN_TEST_CASE(expansion, regular_text_then_variable);
    RUN_TEST_CASE(expansion, variable_then_regular_text);
    RUN_TEST_CASE(expansion, variable_then_escaped_regular_text);
    RUN_TEST_CASE(expansion, variable_then_regular_delim);
    RUN_TEST_CASE(expansion, escaped_variable_does_not_expand);
    RUN_TEST_CASE(expansion, set_builtin_fails_on_bad_key);
    RUN_TEST_CASE(expansion, variable_with_space_expands);
    RUN_TEST_CASE(expansion, variable_with_quoted_and_unquoted_segments);
    RUN_TEST_CASE(expansion, variable_then_escaped_variable);
}
