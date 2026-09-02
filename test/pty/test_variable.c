#define _GNU_SOURCE

#include "unity_fixture.h"
#include "pty_test.h"

TEST_GROUP(variable);

/************ Fixture ************/

const char *home;

TEST_SETUP(variable) {
    new_shell();

    home = getenv("HOME");
    if (!home)
        TEST_FAIL_MESSAGE("could not find HOME env variable\n");

    pty_send("set FOO bar\n");
    pty_ensure(PROMPT);
}

TEST_TEAR_DOWN(variable) {
    delete_shell();
}

/************ Tests ************/

TEST(variable, memtest) {
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

TEST(variable, variable) {
    pty_send("echo $FOO\n");
    pty_ensure("bar\n");
    pty_ensure(PROMPT);
}

TEST(variable, back_to_back_variables) {
    pty_send("echo $FOO$FOO\n");
    pty_ensure("barbar\n");
    pty_ensure(PROMPT);
}

TEST(variable, regular_text_then_variable) {
    pty_send("echo abc$FOO\n");
    pty_ensure("abcbar\n");
    pty_ensure(PROMPT);
}

TEST(variable, variable_then_regular_text) {
    pty_send("echo $FOOabc\n");
    pty_ensure("\n");
    pty_ensure(PROMPT);
}

TEST(variable, variable_then_escaped_regular_text) {
    pty_send("echo $FOO\\abc\n");
    pty_ensure("barabc\n");
    pty_ensure(PROMPT);
}

TEST(variable, variable_then_regular_delim) {
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

TEST(variable, escaped_variable_does_not_expand) {
    pty_send("echo \\$FOO\n");
    pty_ensure("$FOO\n");
    pty_ensure(PROMPT);
}

TEST(variable, set_builtin_fails_on_bad_key) {
    pty_send("set \\$FOO bar\n");
    pty_ensure("set: failed to add variable\n");
    pty_ensure(PROMPT);
}

TEST(variable, variable_with_space_expands) {
    pty_send("set FOO \"b a r\"\n");
    pty_ensure(PROMPT);

    pty_send("echo $FOO\n");
    pty_ensure("b a r\n");
    pty_ensure(PROMPT);
}

TEST(variable, variable_with_quoted_and_unquoted_segments) {
    pty_send("set FOO hello\" beautiful\"' world!'\n");
    pty_ensure(PROMPT);

    pty_send("echo $FOO\n");
    pty_ensure("hello beautiful world!\n");
    pty_ensure(PROMPT);
}

TEST(variable, variable_then_escaped_variable) {
    pty_send("echo $FOO\\$FOO\n");
    pty_ensure("bar$FOO\n");
    pty_ensure(PROMPT);
}

TEST(variable, tilde_expansion) {
    pty_send("echo ~\n");
    pty_ensure(home);
    pty_ensure("\n");
    pty_ensure(PROMPT);
}

TEST(variable, tilde_then_regular_text) {
    pty_send("echo ~hi\n");
    pty_ensure(home);
    pty_ensure("hi\n");
    pty_ensure(PROMPT);
}

TEST(variable, regular_text_then_tilde) {
    pty_send("echo hi~\n");
    pty_ensure("hi");
    pty_ensure(home);
    pty_ensure("\n");
    pty_ensure(PROMPT);
}

TEST(variable, escaped_tilde) {
    pty_send("echo \\~\n");
    pty_ensure("~\n");
    pty_ensure(PROMPT);
}

TEST(variable, regular_text_around_escaped_tilde) {
    pty_send("echo x\\~y\n");
    pty_ensure("x~y\n");
    pty_ensure(PROMPT);
}

TEST(variable, variable_expands_inside_double_quotes) {
    pty_send("echo \"$FOO\"\n");
    pty_ensure("bar\n");
    pty_ensure(PROMPT);
}

TEST(variable, variable_does_not_expand_inside_single_quotes) {
    pty_send("echo '$FOO'\n");
    pty_ensure("$FOO\n");
    pty_ensure(PROMPT);
}

TEST(variable, trailing_dollar_expands_to_empty_string) {
    pty_send("echo hi$\n");
    pty_ensure("hi\n");
    pty_ensure(PROMPT);
}

TEST(variable, variable_name_can_contain_hyphen) {
    pty_send("set FOO-BAR baz\n");
    pty_ensure(PROMPT);

    pty_send("echo $FOO-BAR\n");
    pty_ensure("baz\n");
    pty_ensure(PROMPT);
}

TEST(variable, escaped_var_inside_double_quotes_does_not_expand) {
    pty_send("echo \"\\$FOO\"\n");
    pty_ensure("$FOO\n");
    pty_ensure(PROMPT);
}

TEST(variable, variable_expands_in_command_position) {
    pty_send("set CMD echo\n");
    pty_ensure(PROMPT);

    pty_send("$CMD hello\n");
    pty_ensure("hello\n");
    pty_ensure(PROMPT);
}

TEST(variable, undefined_variable_expands_to_empty_string) {
    pty_send("echo before$DOES_NOT_EXIST.after\n");
    pty_ensure("before.after\n");
    pty_ensure(PROMPT);
}

TEST(variable, tilde_expands_inside_double_quotes) {
    pty_send("echo \"~\"\n");
    pty_ensure(home);
    pty_ensure("\n");
    pty_ensure(PROMPT);
}

TEST(variable, tilde_does_not_expand_inside_single_quotes) {
    pty_send("echo '~'\n");
    pty_ensure("~\n");
    pty_ensure(PROMPT);
}

TEST(variable, double_quoted_variable_then_regular_text) {
    pty_send("echo \"$FOO\"abc\n");
    pty_ensure("barabc\n");
    pty_ensure(PROMPT);
}

TEST(variable, single_quoted_variable_then_expanded_variable) {
    pty_send("echo '$FOO'$FOO\n");
    pty_ensure("$FOObar\n");
    pty_ensure(PROMPT);
}

/************ Test runner ************/

TEST_GROUP_RUNNER(variable) {
    /* Run with -m -g variable -n memtest */
    RUN_TEST_CASE(variable, memtest);

    RUN_TEST_CASE(variable, variable);
    RUN_TEST_CASE(variable, back_to_back_variables);
    RUN_TEST_CASE(variable, regular_text_then_variable);
    RUN_TEST_CASE(variable, variable_then_regular_text);
    RUN_TEST_CASE(variable, variable_then_escaped_regular_text);
    RUN_TEST_CASE(variable, variable_then_regular_delim);
    RUN_TEST_CASE(variable, escaped_variable_does_not_expand);
    RUN_TEST_CASE(variable, set_builtin_fails_on_bad_key);
    RUN_TEST_CASE(variable, variable_with_space_expands);
    RUN_TEST_CASE(variable, variable_with_quoted_and_unquoted_segments);
    RUN_TEST_CASE(variable, variable_then_escaped_variable);
    RUN_TEST_CASE(variable, tilde_expansion);
    RUN_TEST_CASE(variable, tilde_then_regular_text);
    RUN_TEST_CASE(variable, regular_text_then_tilde);
    RUN_TEST_CASE(variable, escaped_tilde);
    RUN_TEST_CASE(variable, regular_text_around_escaped_tilde);
    RUN_TEST_CASE(variable, variable_expands_inside_double_quotes);
    RUN_TEST_CASE(variable, variable_does_not_expand_inside_single_quotes);
    RUN_TEST_CASE(variable, trailing_dollar_expands_to_empty_string);
    RUN_TEST_CASE(variable, variable_name_can_contain_hyphen);
    RUN_TEST_CASE(variable, escaped_var_inside_double_quotes_does_not_expand);
    RUN_TEST_CASE(variable, variable_expands_in_command_position);
    RUN_TEST_CASE(variable, undefined_variable_expands_to_empty_string);
    RUN_TEST_CASE(variable, tilde_expands_inside_double_quotes);
    RUN_TEST_CASE(variable, tilde_does_not_expand_inside_single_quotes);
    RUN_TEST_CASE(variable, double_quoted_variable_then_regular_text);
    RUN_TEST_CASE(variable, single_quoted_variable_then_expanded_variable);
}
