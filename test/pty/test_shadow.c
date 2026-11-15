#include "t_assert.h"
#include "t_fixture.h"
#include "unity_fixture.h"
#include "t_harness.h"

TEST_GROUP(shadow);

/************ Fixture ************/

TEST_SETUP(shadow) {
    test_setup("./seashell", "--clean");
    assert_term(0, 2, PROMPT);
}

TEST_TEAR_DOWN(shadow) {
    test_teardown();
}

/************ Tests ************/

// NOLINTBEGIN(bugprone-suspicious-missing-comma)

TEST(shadow, shadow_displays_latest_history) {
    vt_send_str("echo goodbye world!\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "echo goodbye world!",
        "goodbye world!",
        PROMPT
    );

    vt_send_str("echo hello world!\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "echo hello world!",
        "hello world!",
        PROMPT
    );

    vt_send_str("e");
    assert_term_attr_clear(0, PROMPT_LEN + 1,
        PROMPT HL_NO_CMD(e) HL_SHADOW(cho hello world!)
    );
}

TEST(shadow, shadow_cannot_be_edited) {
    vt_send_str("echo hello world!\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "echo hello world!",
        "hello world!",
        PROMPT
    );

    vt_send_str("echo");
    assert_term_attr_clear(0, PROMPT_LEN + 4,
        PROMPT HL_CMD(echo) HL_SHADOW_RAW(" hello world!")
    );

    vt_send_key(VTERM_KEY_RIGHT, VTERM_MOD_NONE);
    vt_send_key(VTERM_KEY_BACKSPACE, VTERM_MOD_NONE);
    assert_term_attr_clear(0, PROMPT_LEN + 3,
        PROMPT HL_NO_CMD(ech) HL_SHADOW_RAW("o hello world!")
    );
}

TEST(shadow, shadow_shortcut_inserts_editable_string) {
    vt_send_str("echo hello world!\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "echo hello world!",
        "hello world!",
        PROMPT
    );

    vt_send_str("echo");
    vt_send_ch('e', VTERM_MOD_CTRL);
    assert_term_attr_clear(0, PROMPT_LEN + 17,
        PROMPT HL_CMD(echo) " hello world!"
    );

    vt_send_key(VTERM_KEY_BACKSPACE, VTERM_MOD_NONE);
    vt_send_str("\n");
    assert_term_attr(2, PROMPT_LEN,
        PROMPT HL_CMD(echo) " hello world",
        "hello world",
        PROMPT
    );
}

TEST(shadow, shadow_wrapping_and_unwrapping_on_resize) {
    vt_send_str("echo hello world!\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "echo hello world!",
        "hello world!",
        PROMPT
    );

    vt_send_str("echo");
    assert_term_attr_clear(0, PROMPT_LEN + 4,
        PROMPT HL_CMD(echo) HL_SHADOW_RAW(" hello world!")
    );

    test_resize_term(5, 5);
    assert_term_attr_clear(1, 1,
        PROMPT HL_CMD(ech),
        HL_CMD(o) HL_SHADOW_RAW(" hel"),
        HL_SHADOW(lo wo),
        HL_SHADOW(rld!)
    );

    test_resize_term(15, 60);
    assert_term_attr_clear(0, PROMPT_LEN + 4,
        PROMPT HL_CMD(echo) HL_SHADOW_RAW(" hello world!")
    );
}

// NOLINTEND(bugprone-suspicious-missing-comma)

/************ Test runner ************/

TEST_GROUP_RUNNER(shadow) {
    RUN_TEST_CASE(shadow, shadow_displays_latest_history);
    RUN_TEST_CASE(shadow, shadow_cannot_be_edited);
    RUN_TEST_CASE(shadow, shadow_shortcut_inserts_editable_string);
    RUN_TEST_CASE(shadow, shadow_wrapping_and_unwrapping_on_resize);
}
