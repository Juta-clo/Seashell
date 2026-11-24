#include "unity_fixture.h"
#include "t_assert.h"
#include <fcntl.h>

TEST_GROUP(substitution);

/************ Fixture ************/

TEST_SETUP(substitution) {
    test_setup("./sea", "--clean");
    assert_term(0, 2, PROMPT);
}

TEST_TEAR_DOWN(substitution) {
    test_teardown();
}

/************ Tests ************/

// NOLINTBEGIN(bugprone-suspicious-missing-comma)

TEST(substitution, simple_command_substitution) {
    vt_send_str("echo $(printf foo)\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "echo $(printf foo)",
        "foo",
        PROMPT
    );
}

TEST(substitution, command_substitution_with_andor_chain) {
    vt_send_str("echo $(true && echo foo)\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "echo $(true && echo foo)",
        "foo",
        PROMPT
    );
}

TEST(substitution, multiple_command_substitutions) {
    vt_send_str("echo $(printf a) $(printf b)\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "echo $(printf a) $(printf b)",
        "a b",
        PROMPT
    );
}

TEST(substitution, multiple_command_substitutions_in_one_part) {
    vt_send_str("echo $(printf a)$(printf b)\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "echo $(printf a)$(printf b)",
        "ab",
        PROMPT
    );
}

TEST(substitution, cmd_sub_within_subshell) {
    vt_send_str("(echo $(printf foo))\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "(echo $(printf foo))",
        "foo",
        PROMPT
    );
}

TEST(substitution, cmd_sub_as_redirection_target) {
    vt_send_str("echo foo > $(printf /tmp/shtest)\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "echo foo > $(printf /tmp/shtest)",
        PROMPT
    );

    vt_send_str("cat /tmp/shtest\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "cat /tmp/shtest",
        "foo",
        PROMPT
    );
}

TEST(substitution, cmd_sub_embedded_in_string) {
    vt_send_str("echo \"hello $(printf world!)\"\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "echo \"hello $(printf world!)\"",
        "hello world!",
        PROMPT
    );
}

TEST(substitution, cmd_sub_in_quotes_with_quotes) {
    vt_send_str("echo \"$(printf \"%s\" foo)\"\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "echo \"$(printf \"%%s\" foo)\"",
        "foo",
        PROMPT
    );
}

TEST(substitution, builtin_with_command_substitution) {
    vt_send_str("set tmp_dir $(echo /tmp)\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "set tmp_dir $(echo /tmp)",
        PROMPT
    );

    vt_send_str("echo $tmp_dir\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "echo $tmp_dir",
        "/tmp",
        PROMPT
    );
}

TEST(substitution, cmd_sub_with_nested_quoted_cmd_sub) {
    vt_send_str("echo \"$(echo \"$(printf foo)\")\"\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "echo \"$(echo \"$(printf foo)\")\"",
        "foo",
        PROMPT
    );
}

TEST(substitution, process_substitution_simple) {
    vt_send_str("cat <(echo foo)\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "cat <(echo foo)",
        "foo",
        PROMPT
    );
}

TEST(substitution, process_substitution_multiple) {
    vt_send_str("cat <(echo foo) <(echo bar)\n");
    assert_term_clear(3, PROMPT_LEN,
        PROMPT "cat <(echo foo) <(echo bar)",
        "foo",
        "bar",
        PROMPT
    );
}

TEST(substitution, interupting_command_substitution) {
    vt_send_str("echo $(write_sleep >&2)\n");
    assert_term(2, 0,
        PROMPT "echo $(write_sleep >&2)",
        "x"
    );

    vt_send_ch('c', VTERM_MOD_CTRL);
    assert_term_clear(3, PROMPT_LEN,
        PROMPT "echo $(write_sleep >&2)",
        "x",
        "^C%s",
        PROMPT
    );
}

TEST(substitution, interrupting_propogates_exit_status) {
    vt_send_str("echo $(write_sleep >&2) || echo unexpected\n");
    assert_term(2, 0,
        PROMPT "echo $(write_sleep >&2) || echo unexpected",
        "x"
    );

    vt_send_ch('c', VTERM_MOD_CTRL);
    assert_term_clear(3, PROMPT_LEN,
        PROMPT "echo $(write_sleep >&2) || echo unexpected",
        "x",
        "^C%s",
        PROMPT
    );

    vt_send_str("echo $?\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo $?",
        "130",
        PROMPT
    );
}

TEST(substitution, interactive_stop_is_ignored) {
    vt_send_str("$(write_sleep >&2)\n");
    assert_term(2, 0,
        PROMPT "$(write_sleep >&2)",
        "x"
    );

    vt_send_ch('z', VTERM_MOD_CTRL);
    assert_term(2, 2,
        PROMPT "$(write_sleep >&2)",
        "x",
        "^Z"
    );

    vt_send_ch('c', VTERM_MOD_CTRL);
    assert_term(3, PROMPT_LEN,
        PROMPT "$(write_sleep >&2)",
        "x",
        "^Z^C%s",
        PROMPT
    );
}

TEST(substitution, substitution_pipe_does_not_deadlock) {
    vt_send_str("set big \"$(fill_pipe)\" && echo done\n");

    assert_term_clear(2, PROMPT_LEN,
        PROMPT "set big \"$(fill_pipe)\" && echo done",
        "done",
        PROMPT
    );
}

TEST(substitution, empty_substitution_does_not_create_empty_arg) {
    vt_send_str("$()\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "$()",
        PROMPT
    );
}

TEST(substitution, user_redirection_does_not_overwrite_proc_sub) {
    vt_send_str("cat <(echo foo) 3</dev/null\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "cat <(echo foo) 3</dev/null",
        "foo",
        PROMPT
    );
}

TEST(substitution, unquoted_substitution_splits_on_whitespace) {
    vt_send_str("set foo $(echo a b)\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "set foo $(echo a b)",
        "set: too many arguments",
        PROMPT
    );
}

TEST(substitution, quoted_substitution_does_not_split) {
    vt_send_str("set foo \"$(echo a b)\"\n");
    assert_term_clear(1, PROMPT_LEN,
        PROMPT "set foo \"$(echo a b)\"",
        PROMPT
    );
}

TEST(substitution, word_split_parts_are_glob_candidates) {
    vt_send_str("printf '<%s>\\n' $(echo /tm\\* /tm\\*)\n");
    assert_term_clear(3, PROMPT_LEN,
        PROMPT "printf '<%%s>\\n' $(echo /tm\\* /tm\\*)",
        "</tmp>",
        "</tmp>",
        PROMPT
    );
}

TEST(substitution, word_split_with_prefix_and_suffix) {
    vt_send_str("printf '<%s>\\n' pre$(echo a b)suf\n");
    assert_term_clear(3, PROMPT_LEN,
        PROMPT "printf '<%%s>\\n' pre$(echo a b)suf",
        "<prea>",
        "<bsuf>",
        PROMPT
    );
}

TEST(substitution, word_split_seq_program) {
    vt_send_str("printf '<%s>\\n' $(seq 1 3)\n");
    assert_term_clear(4, PROMPT_LEN,
        PROMPT "printf '<%%s>\\n' $(seq 1 3)",
        "<1>",
        "<2>",
        "<3>",
        PROMPT
    );
}

TEST(substitution, cmd_sub_with_quoted_prefix_and_suffix) {
    vt_send_str("printf '<%s>\\n' 'pre'$(echo a b)\"suf\"\n");
    assert_term_clear(3, PROMPT_LEN,
        PROMPT "printf '<%%s>\\n' 'pre'$(echo a b)\"suf\"",
        "<prea>",
        "<bsuf>",
        PROMPT
    );

    vt_send_str("printf '<%s>\\n' \"pre\"$(echo a b)'suf'\n");
    assert_term_clear(3, PROMPT_LEN,
        PROMPT "printf '<%%s>\\n' \"pre\"$(echo a b)'suf'",
        "<prea>",
        "<bsuf>",
        PROMPT
    );
}

// NOLINTEND(bugprone-suspicious-missing-comma)

/************ Test runner ************/

TEST_GROUP_RUNNER(substitution) {
    RUN_TEST_CASE(substitution, simple_command_substitution);
    RUN_TEST_CASE(substitution, command_substitution_with_andor_chain);
    RUN_TEST_CASE(substitution, multiple_command_substitutions);
    RUN_TEST_CASE(substitution, cmd_sub_within_subshell);
    RUN_TEST_CASE(substitution, cmd_sub_as_redirection_target);
    RUN_TEST_CASE(substitution, cmd_sub_embedded_in_string);
    RUN_TEST_CASE(substitution, cmd_sub_in_quotes_with_quotes);
    RUN_TEST_CASE(substitution, builtin_with_command_substitution);
    RUN_TEST_CASE(substitution, cmd_sub_with_nested_quoted_cmd_sub);
    RUN_TEST_CASE(substitution, process_substitution_simple);
    RUN_TEST_CASE(substitution, process_substitution_multiple);
    RUN_TEST_CASE(substitution, interupting_command_substitution);
    RUN_TEST_CASE(substitution, interrupting_propogates_exit_status);
    RUN_TEST_CASE(substitution, interactive_stop_is_ignored);
    RUN_TEST_CASE(substitution, substitution_pipe_does_not_deadlock);
    RUN_TEST_CASE(substitution, empty_substitution_does_not_create_empty_arg);
    RUN_TEST_CASE(substitution, user_redirection_does_not_overwrite_proc_sub);
    RUN_TEST_CASE(substitution, unquoted_substitution_splits_on_whitespace);
    RUN_TEST_CASE(substitution, quoted_substitution_does_not_split);
    RUN_TEST_CASE(substitution, word_split_parts_are_glob_candidates);
    RUN_TEST_CASE(substitution, word_split_with_prefix_and_suffix);
    RUN_TEST_CASE(substitution, word_split_seq_program);
    RUN_TEST_CASE(substitution, cmd_sub_with_quoted_prefix_and_suffix);
}
