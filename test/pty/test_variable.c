#include "unity_fixture.h"
#include "pty_test.h"

TEST_GROUP(variable);

/************ Fixture ************/

char *home;

TEST_SETUP(variable) {
    new_shell();

    home = getenv("HOME");
    if (!home)
        TEST_FAIL_MESSAGE("could not find HOME env variable\n");

    vterm_send_str("set FOO bar\n");

    assert_term(
        PROMPT "set FOO bar",
        PROMPT
    );
}

TEST_TEAR_DOWN(variable) {
    delete_shell();
}

/************ Tests ************/

TEST(variable, variable) {
    vterm_send_str("echo $FOO\n");
    assert_term(
        PROMPT "set FOO bar",
        PROMPT "echo $FOO",
        "bar",
        PROMPT
    );
}

TEST(variable, back_to_back_variables) {
    vterm_send_str("echo $FOO$FOO\n");
    assert_term(
        PROMPT "set FOO bar",
        PROMPT "echo $FOO$FOO",
        "barbar",
        PROMPT
    );
}

TEST(variable, regular_text_then_variable) {
    vterm_send_str("echo abc$FOO\n");
    assert_term(
        PROMPT "set FOO bar",
        PROMPT "echo abc$FOO",
        "abcbar",
        PROMPT
    );
}

TEST(variable, variable_then_regular_text) {
    vterm_send_str("echo $FOOabc\n");
    assert_term(
        PROMPT "set FOO bar",
        PROMPT "echo $FOOabc",
        " ",
        PROMPT
    );
}

TEST(variable, variable_then_escaped_regular_text) {
    vterm_send_str("echo $FOO\\abc\n");
    assert_term(
        PROMPT "set FOO bar",
        PROMPT "echo $FOO\\abc",
        "barabc",
        PROMPT
    );
}

TEST(variable, variable_then_regular_delim) {
    vterm_send_str("/bin/echo $FOO.txt $FOO/home $FOO bee\n");
    assert_term(
        PROMPT "set FOO bar",
        PROMPT "/bin/echo $FOO.txt $FOO/home $FOO bee",
        "bar.txt bar/home bar bee",
        PROMPT
    );
}

TEST(variable, escaped_variable_does_not_expand) {
    vterm_send_str("echo \\$FOO\n");
    assert_term(
        PROMPT "set FOO bar",
        PROMPT "echo \\$FOO",
        "$FOO",
        PROMPT
    );
}

TEST(variable, set_builtin_fails_on_bad_key) {
    vterm_send_str("set \\$BAD bar\n");
    assert_term(
        PROMPT "set FOO bar",
        PROMPT "set \\$BAD bar",
        "set: failed to add variable",
        PROMPT
    );
}

TEST(variable, variable_with_space_expands) {
    vterm_send_str("set FOO \"b a r\"\n");
    assert_term(
        PROMPT "set FOO bar",
        PROMPT "set FOO \"b a r\"",
        PROMPT
    );

    vterm_send_str("echo $FOO\n");
    assert_term(
        PROMPT "set FOO bar",
        PROMPT "set FOO \"b a r\"",
        PROMPT "echo $FOO",
        "b a r",
        PROMPT
    );
}

TEST(variable, variable_with_quoted_and_unquoted_segments) {
    vterm_send_str("set FOO hello\" beautiful\"' world!'\n");
    assert_term(
        PROMPT "set FOO bar",
        PROMPT "set FOO hello\" beautiful\"' world!",
        PROMPT
    );

    vterm_send_str("echo $FOO\n");
    assert_term(
        PROMPT "set FOO bar",
        PROMPT "set FOO hello\" beautiful\"' world!",
        PROMPT "echo $FOO",
        "hello beautiful world!",
        PROMPT
    );
}

TEST(variable, variable_then_escaped_variable) {
    vterm_send_str("echo $FOO\\$FOO\n");
    assert_term(
        PROMPT "set FOO bar",
        PROMPT "echo $FOO\\$FOO",
        "bar$FOO",
        PROMPT
    );
}

TEST(variable, tilde_expansion) {
    vterm_send_str("echo ~\n");
    assert_term(
        PROMPT "set FOO bar",
        PROMPT "echo ~",
        home,
        PROMPT
    );
}

TEST(variable, escaped_tilde) {
    vterm_send_str("echo \\~\n");
    assert_term(
        PROMPT "set FOO bar",
        PROMPT "echo \\~",
        "~",
        PROMPT
    );
}

TEST(variable, regular_text_around_escaped_tilde) {
    vterm_send_str("echo x\\~y\n");
    assert_term(
        PROMPT "set FOO bar",
        PROMPT "echo x\\~y",
        "x~y",
        PROMPT
    );
}

TEST(variable, variable_expands_inside_double_quotes) {
    vterm_send_str("echo \"$FOO\"\n");
    assert_term(
        PROMPT "set FOO bar",
        PROMPT "echo \"$FOO\"",
        "bar",
        PROMPT
    );
}

/************ Test runner ************/

TEST_GROUP_RUNNER(variable) {
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
    RUN_TEST_CASE(variable, escaped_tilde);
    RUN_TEST_CASE(variable, regular_text_around_escaped_tilde);
    RUN_TEST_CASE(variable, variable_expands_inside_double_quotes);
}
