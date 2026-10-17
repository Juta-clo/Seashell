#include "unity_fixture.h"
#include "pty_test.h"

TEST_GROUP(resize);

/************ Fixture ************/

TEST_SETUP(resize) {
    test_setup();
}

TEST_TEAR_DOWN(resize) {
    test_teardown();
}

/************ Tests ************/

// NOLINTBEGIN(bugprone-suspicious-missing-comma)

TEST(resize, resize_that_causes_cursor_wrap) {
    test_resize_term(10, 1);
    vt_send_str("x");
    assert_term(PROMPT_LEN + 1, 0,
        ">",
        " ",
        "x",
        " "
    );
}

TEST(resize, resize_that_causes_cursor_wrap_then_unwrap) {
    /* We move the cursor to the left to prevent the assert_cursor
     * call from prematurely passing. If the offset is at the end
     * then input_draw, will leave it at the same spot. input_update
     * sends a final cursor move request in order to move the cursor
     * to the offset in the cases that it is different. If that final
     * move request is delayed until after the second resize it will
     * caues bad things to happen */

    test_resize_term(10, 1);

    vt_send_str("acd");
    vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    assert_term(PROMPT_LEN + 1, 0,
        ">",
        " ",
        "a",
        "c",
        "d",
        " "
    );

    test_resize_term(10, 10);

    vt_send_str("b");
    assert_term(0, PROMPT_LEN + 2,
        PROMPT "abcd"
    );
}

TEST(resize, resize_with_cursor_on_row_less_then_end_of_line) {
    vt_send_str("\n\n\n\nbcdefghijklmnopqrstuvwxyz");
    vt_send_key(VTERM_KEY_ESCAPE, VTERM_MOD_NONE);
    vt_send_str("I");
    assert_term(4, PROMPT_LEN,
        PROMPT,
        PROMPT,
        PROMPT,
        PROMPT,
        PROMPT "bcdefghijklmnopqrstuvwxyz"
    );

    test_resize_term(10, 10);

    vt_send_str("a");
    assert_term(4, PROMPT_LEN + 1,
        PROMPT,
        PROMPT,
        PROMPT,
        PROMPT,
        PROMPT "abcdefgh",
        "ijklmnopqr",
        "stuvwxyz"
    );
}

TEST(resize, insert_text_in_pager) {
    test_resize_term(2, 5);

    vt_send_str("abcdefgh");
    assert_term(1, 0,
        ">..."
    );

    vt_send_str("ijk");
    vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    assert_term(1, 1,
        ">...",
        "ijk"
    );
}

TEST(resize, insert_text_that_enters_pager) {
    test_resize_term(5, 1);

    vt_send_str("xxx");
    assert_term(1, 0,
        ">",
        " "
    );
}

TEST(resize, insert_that_causes_emulator_scroll) {
    test_resize_term(4, PROMPT_LEN + 1);

    vt_send_str("\n\n\nx");
    assert_term(3, 0,
        PROMPT,
        PROMPT,
        PROMPT "x",
        " "
    );
}

TEST(resize, resize_to_larger_row_size_while_in_pager) {
    test_resize_term(5, 4);

    vt_send_str("hello, beautiful world");
    assert_term(2, 0,
        ">...",
        "orld",
        " "
    );

    test_resize_term(10, 4);

    vt_send_str("!");
    assert_term(6, 1,
        PROMPT "he",
        "llo,",
        " bea",
        "utif",
        "ul w",
        "orld",
        "!"
    );
}

TEST(resize, resize_to_larger_column_size_while_in_pager) {
    test_resize_term(5, 1);

    vt_send_str("abcdef");
    vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    assert_term(2, 0,
        ">",
        "d",
        "e",
        "f",
        " "
    );

    test_resize_term(5, 10);

    assert_term(0, PROMPT_LEN + 4,
        PROMPT "abcdef"
    );
}

TEST(resize, resize_that_enters_pager) {
    vt_send_str("hello, beautiful world!");
    assert_term(0, 25,
        PROMPT "hello, beautiful world!"
    );

    test_resize_term(5, 4);

    assert_term(2, 1,
        ">...",
        "orld",
        "!"
    );
}

TEST(resize, clearing_line_from_within_pager) {
    test_resize_term(2, 4);

    vt_send_str("abcdefghi");
    vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    assert_term(1, 1,
        ">...",
        "ghi"
    );

    test_resize_term(10, 10);

    vt_send_ch('u', VTERM_MOD_CTRL);
    assert_term(0, PROMPT_LEN,
        PROMPT "hi"
    );
}

TEST(resize, resize_after_emulator_scroll) {
    test_resize_term(6, 4);

    vt_send_str("\n\n\n\nabcdef");
    vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    vt_send_key(VTERM_KEY_LEFT, VTERM_MOD_NONE);
    assert_term(4, 2,
        PROMPT,
        PROMPT,
        PROMPT,
        PROMPT "ab",
        "cdef",
        " "
    );

    test_resize_term(6, 2);
    assert_term(4, 0,
        PROMPT,
        PROMPT,
        "ab",
        "cd",
        "ef",
        " "
    );

    test_resize_term(6, 4);
    assert_term(2, 2,
        PROMPT,
        PROMPT "ab",
        "cdef",
        " "
    );

    /* It would be nice to be able to query libvterms
     * scrollback to assert the right amount of lines
     * but it doesn't expose it. Another reason to switch
     * to libghostty-vt */
}

TEST(resize, pager_with_only_one_row) {
    test_resize_term(1, 10);

    vt_send_str("hello, beautiful world!");
    assert_term(0, 4,
        ">..."
    );
}

// NOLINTEND(bugprone-suspicious-missing-comma)

/************ Test runner ************/

TEST_GROUP_RUNNER(resize) {
    RUN_TEST_CASE(resize, resize_that_causes_cursor_wrap);
    RUN_TEST_CASE(resize, resize_that_causes_cursor_wrap_then_unwrap);
    RUN_TEST_CASE(resize, resize_with_cursor_on_row_less_then_end_of_line);
    RUN_TEST_CASE(resize, insert_text_in_pager);
    RUN_TEST_CASE(resize, insert_text_that_enters_pager);
    RUN_TEST_CASE(resize, insert_that_causes_emulator_scroll);
    RUN_TEST_CASE(resize, resize_to_larger_row_size_while_in_pager);
    RUN_TEST_CASE(resize, resize_to_larger_column_size_while_in_pager);
    RUN_TEST_CASE(resize, resize_that_enters_pager);
    RUN_TEST_CASE(resize, clearing_line_from_within_pager);
    RUN_TEST_CASE(resize, resize_after_emulator_scroll);
    RUN_TEST_CASE(resize, pager_with_only_one_row);
}
