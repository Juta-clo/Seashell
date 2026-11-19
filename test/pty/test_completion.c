#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include "t_assert.h"
#include "t_fixture.h"
#include "unity_fixture.h"

TEST_GROUP(completion);

/************ Fixture ************/

TEST_SETUP(completion) {
    test_setup("./seashell", "--clean");
    assert_term(0, 2, PROMPT);

    if (mkdir("/tmp/sh-tmp", 0700) == -1 && errno != EEXIST)
        xtest_fatal("mkdir");
}

TEST_TEAR_DOWN(completion) {
    test_teardown();

    if (system("rm -rf /tmp/sh-tmp") != 0)
        xtest_fatal("system: %m");
}

/************ Tests ************/

// NOLINTBEGIN(bugprone-suspicious-missing-comma)


TEST(completion, with_no_line) {
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

TEST(completion, on_filename_with_whitespace) {
    close(open("/tmp/sh-tmp/file with spaces", O_RDWR | O_CREAT, 0600));

    vt_send_str("stat /tmp/sh-tmp/file\t");
    assert_term(0, PROMPT_LEN + 35,
        PROMPT "stat /tmp/sh-tmp/file\\ with\\ spaces");
}

TEST(completion, cycles_results) {
    close(open("/tmp/sh-tmp/file1", O_RDWR | O_CREAT, 0600));
    close(open("/tmp/sh-tmp/foo", O_RDWR | O_CREAT, 0600));
    close(open("/tmp/sh-tmp/foo bar", O_RDWR | O_CREAT, 0600));

    char *lines[3] = {
        PROMPT "stat /tmp/sh-tmp/file1",
        PROMPT "stat /tmp/sh-tmp/foo",
        PROMPT "stat /tmp/sh-tmp/foo\\ bar"
    };

    vt_send_str("stat /tmp/sh-tmp/f");

    for (size_t i = 0; i < ARRLEN(lines) * 2; ++i) {
        char *exp = lines[i % ARRLEN(lines)];

        vt_send_str("\t");
        assert_term(0, strlen(exp), exp);
    }
}

TEST(completion, forward_slash_confirms_completion) {
    close(open("/tmp/sh-tmp/foo", O_RDWR | O_CREAT, 0600));

    vt_send_str("stat /tmp/sh-tmp/f\t");
    assert_term(0, PROMPT_LEN + 20,
        PROMPT "stat /tmp/sh-tmp/foo");

    vt_send_str("/ bar");
    assert_term(0, PROMPT_LEN + 24, PROMPT "stat /tmp/sh-tmp/foo bar");
}

TEST(completion, first_word_after_pipeline_completes_path) {
    vt_send_str("ls -a | gre\t");
    assert_term(0, PROMPT_LEN + 12,
        PROMPT "ls -a | grep"
    );
}

TEST(completion, first_word_after_andor_uses_path_completion) {
    vt_send_str("ls -a && gre\t");
    assert_term(0, PROMPT_LEN + 13,
        PROMPT "ls -a && grep"
    );

    vt_send_str("\b\b\b\b\b\b\b|| grep\t");
    assert_term(0, PROMPT_LEN + 13,
        PROMPT "ls -a || grep"
    );
}

TEST(completion, command_before_pipe) {
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

TEST(completion, file_target_redirect_after_subshell) {
    vt_send_str("(echo hi) > /tmp/sh-tm");
    vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN + 24,
        PROMPT "(echo hi) > /tmp/sh-tmp/"
    );
}

TEST(completion, glob_in_completion_glob) {
    vt_send_str("stat /tmp/sh*tm");
    vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN + 17,
        PROMPT "stat /tmp/sh-tmp/"
    );
}

TEST(completion, on_redirect_with_no_target) {
    vt_send_str("echo >");
    vt_send_ch('a', VTERM_MOD_CTRL);
    vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN,
        PROMPT "echo >"
    );
}

TEST(completion, command_in_subshell) {
    vt_send_str("(gre)");
    vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN + 5,
        PROMPT "(grep)"
    );
}

TEST(completion, command_in_incomplete_subshell) {
    vt_send_str("(gre");
    vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN + 5,
        PROMPT "(grep"
    );
}

TEST(completion, command_in_incomplete_subshell_before_pipe) {
    vt_send_str("(gre | a");
    for (size_t i = 0; i < 4; ++i)
        vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN + 5,
        PROMPT "(grep | a"
    );
}

TEST(completion, command_in_subshell_after_pipe) {
    vt_send_str("a | (gre)");
    vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN + 9,
        PROMPT "a | (grep)"
    );
}

TEST(completion, on_forward_slash) {
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

TEST(completion, on_relative_path) {
    vt_send_str("cd ./asse");
    vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
    assert_term_clear(0, PROMPT_LEN + 12,
        PROMPT "cd ./assets/"
    );
}

TEST(completion, relative_path_no_relative_specifier) {
    vt_send_str("cd asse");
    vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
    assert_term_clear(0, PROMPT_LEN + 10,
        PROMPT "cd assets/"
    );
}

TEST(completion, relative_path_with_prefix) {
    vt_send_str("cd ./src/main.");
    vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
    assert_term_clear(0, PROMPT_LEN + 15,
        PROMPT "cd ./src/main.c"
    );
}

TEST(completion, prefix_with_spaces_that_are_already_escaped) {
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

TEST(completion, wildcard_directory_cycles_concrete_paths) {
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

TEST(completion, trailing_slash_on_unreadable_directory) {
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

TEST(completion, directory_completion_from_root_directory) {
    vt_send_str("cd /tm");
    vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
    assert_term(0, PROMPT_LEN + 8,
        PROMPT "cd /tmp/"
    );
}

TEST(completion, completion_works_after_parse_error) {
    vt_send_str("| cd /tm");
    vt_send_key(VTERM_KEY_TAB, VTERM_MOD_NONE);
    assert_term_attr(0, PROMPT_LEN + 10,
        PROMPT HL_SYNTAX(|) " " HL_CMD(cd) " " HL_PATH(/tmp/)
    );
}

// NOLINTEND(bugprone-suspicious-missing-comma)

/************ Test runner ************/

TEST_GROUP_RUNNER(completion) {
    RUN_TEST_CASE(completion, with_no_line);
    RUN_TEST_CASE(completion, on_filename_with_whitespace);
    RUN_TEST_CASE(completion, cycles_results);
    RUN_TEST_CASE(completion, forward_slash_confirms_completion);
    RUN_TEST_CASE(completion, first_word_after_pipeline_completes_path);
    RUN_TEST_CASE(completion, first_word_after_andor_uses_path_completion);
    RUN_TEST_CASE(completion, command_before_pipe);
    RUN_TEST_CASE(completion, file_target_redirect_after_subshell);
    RUN_TEST_CASE(completion, glob_in_completion_glob);
    RUN_TEST_CASE(completion, on_redirect_with_no_target);
    RUN_TEST_CASE(completion, command_in_subshell);
    RUN_TEST_CASE(completion, command_in_incomplete_subshell);
    RUN_TEST_CASE(completion, command_in_subshell_after_pipe);
    RUN_TEST_CASE(completion, on_forward_slash);
    RUN_TEST_CASE(completion, on_relative_path);
    RUN_TEST_CASE(completion, relative_path_no_relative_specifier);
    RUN_TEST_CASE(completion, relative_path_with_prefix);
    RUN_TEST_CASE(completion, prefix_with_spaces_that_are_already_escaped);
    RUN_TEST_CASE(completion, wildcard_directory_cycles_concrete_paths);
    RUN_TEST_CASE(completion, trailing_slash_on_unreadable_directory);
    RUN_TEST_CASE(completion, directory_completion_from_root_directory);
    RUN_TEST_CASE(completion, completion_works_after_parse_error);
}
