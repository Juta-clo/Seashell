#include "t_harness.h" // IWYU pragma: keep
#include "t_fixture.h"
#include "unity_fixture.h"
#include "t_assert.h"

TEST_GROUP(variable);

/************ Fixture ************/

char *home;

TEST_SETUP(variable) {
    test_setup("./seashell", "--clean");
    assert_term(0, 2, PROMPT);

    home = getenv("HOME");
    if (!home)
        PTY_TEST_FAIL("could not find HOME env variable\n");

    vt_send_str("set FOO bar\n");

    assert_term_clear(1, PROMPT_LEN,
        PROMPT "set FOO bar",
        PROMPT
    );
}

TEST_TEAR_DOWN(variable) {
    test_teardown();
}

/************ Tests ************/

// NOLINTBEGIN(bugprone-suspicious-missing-comma)

TEST(variable, variable) {
    vt_send_str("echo $FOO\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo $FOO",
        "bar",
        PROMPT
    );
}

TEST(variable, back_to_back_variables) {
    vt_send_str("echo $FOO$FOO\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo $FOO$FOO",
        "barbar",
        PROMPT
    );
}

TEST(variable, regular_text_then_variable) {
    vt_send_str("echo abc$FOO\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo abc$FOO",
        "abcbar",
        PROMPT
    );
}

TEST(variable, variable_then_regular_text) {
    vt_send_str("echo $FOOabc\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo $FOOabc",
        "",
        PROMPT
    );
}

TEST(variable, variable_then_escaped_regular_text) {
    vt_send_str("echo $FOO\\abc\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo $FOO\\abc",
        "barabc",
        PROMPT
    );
}

TEST(variable, variable_then_regular_delim) {
    vt_send_str("/bin/echo $FOO.txt $FOO/home $FOO bee\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "/bin/echo $FOO.txt $FOO/home $FOO bee",
        "bar.txt bar/home bar bee",
        PROMPT
    );
}

TEST(variable, escaped_variable_does_not_expand) {
    vt_send_str("echo \\$FOO\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo \\$FOO",
        "$FOO",
        PROMPT
    );
}

TEST(variable, set_builtin_fails_on_bad_key) {
    vt_send_str("set \\$BAD bar\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "set \\$BAD bar",
        "set: failed to add variable",
        PROMPT
    );
}

TEST(variable, variable_with_space_expands) {
    vt_send_str("set FOO \"b a r\"\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "set FOO \"b a r\"",
        PROMPT
    );

    vt_send_str("echo $FOO\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo $FOO",
        "b a r",
        PROMPT
    );
}

TEST(variable, variable_with_quoted_and_unquoted_segments) {
    vt_send_str("set FOO hello\" beautiful\"' world!'\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "set FOO hello\" beautiful\"' world!'",
        PROMPT
    );

    vt_send_str("echo $FOO\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo $FOO",
        "hello beautiful world!",
        PROMPT
    );
}

TEST(variable, variable_then_escaped_variable) {
    vt_send_str("echo $FOO\\$FOO\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo $FOO\\$FOO",
        "bar$FOO",
        PROMPT
    );
}

TEST(variable, tilde_expansion) {
    vt_send_str("echo ~\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo ~",
        home,
        PROMPT
    );
}

TEST(variable, escaped_tilde) {
    vt_send_str("echo \\~\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo \\~",
        "~",
        PROMPT
    );
}

TEST(variable, regular_text_around_escaped_tilde) {
    vt_send_str("echo x\\~y\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo x\\~y",
        "x~y",
        PROMPT
    );
}

TEST(variable, variable_expands_inside_double_quotes) {
    vt_send_str("echo \"$FOO\"\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo \"$FOO\"",
        "bar",
        PROMPT
    );
}

TEST(variable, exit_status_variable) {
    vt_send_str("echo $?\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "echo $?",
        "0",
        PROMPT
    );

    vt_send_str("false\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "false",
        PROMPT
    );

    vt_send_str("echo $?\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo $?",
        "1",
        PROMPT
    );
}

TEST(variable, unset_builtin_with_exit_status_variable) {
    vt_send_str("unset ?\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "unset ?",
        PROMPT
    );

    vt_send_str("echo $?\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo $?",
        "0",
        PROMPT
    );
}

TEST(variable, set_builtin_with_exit_status_variable) {
    vt_send_str("set ? FOO\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "set ? FOO",
        PROMPT
    );

    vt_send_str("echo $?\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo $?",
        "0",
        PROMPT
    );
}

// NOLINTEND(bugprone-suspicious-missing-comma)
//
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
    RUN_TEST_CASE(variable, exit_status_variable);
    RUN_TEST_CASE(variable, unset_builtin_with_exit_status_variable);
    RUN_TEST_CASE(variable, set_builtin_with_exit_status_variable);
}
