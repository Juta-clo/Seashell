#include "unity_fixture.h"
#include "pty_test.h"

TEST_GROUP(resize);

/************ Fixture ************/

TEST_SETUP(resize) {
    new_shell();
}

TEST_TEAR_DOWN(resize) {
    delete_shell();
}

/************ Tests ************/

// NOLINTBEGIN(bugprone-suspicious-missing-comma)

TEST(resize, resize_that_causes_cursor_wrap) {
}

TEST(resize, resize_that_causes_cursor_wrap_then_unwrap) {
}

TEST(resize, resize_with_cursor_on_row_less_then_end_of_line) {
    vterm_send_str("\n\n\n\nbcdefghijklmnopqrstuvwxyz");
    vterm_send_key(VTERM_KEY_ESCAPE, VTERM_MOD_NONE);
    vterm_send_str("I");
    assert_cursor(4, PROMPT_LEN);
    assert_term(
        PROMPT,
        PROMPT,
        PROMPT,
        PROMPT,
        PROMPT "bcdefghijklmnopqrstuvwxyz"
    );

    test_resize_term(10, 10);
    vterm_send_str("a");
    assert_cursor(4, PROMPT_LEN + 1);
    assert_term(
        PROMPT,
        PROMPT,
        PROMPT,
        PROMPT,
        PROMPT "abcdefghijklmnopqrstuvwxyz"
    );
}

// NOLINTEND(bugprone-suspicious-missing-comma)

/************ Test runner ************/

TEST_GROUP_RUNNER(resize) {
    RUN_TEST_CASE(resize, resize_that_causes_cursor_wrap);
    RUN_TEST_CASE(resize, resize_that_causes_cursor_wrap_then_unwrap);
    RUN_TEST_CASE(resize, resize_with_cursor_on_row_less_then_end_of_line);
}
