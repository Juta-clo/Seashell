#include <pty.h>

#include "t_harness.h" // IWYU pragma: keep
#include "t_fixture.h"
#include "unity_fixture.h"
#include "t_assert.h"

TEST_GROUP(termios);

/************ Fixture ************/

static struct termios tio;

static void init_tio(void) {
    int mfd, sfd;
    if (openpty(&mfd, &sfd, NULL, NULL, NULL) == -1)
        xtest_fatal("openpty: %m");

    if (tcgetattr(sfd, &tio) == -1)
        xtest_fatal("tcgetattr: %m");

    if (close(mfd) == -1)
        xtest_fatal("close: %m");
    if (close(sfd) == -1)
        xtest_fatal("close: %m");
}

/* this should mirror printty's cbreak function */
struct termios printty_cbreak_tio(struct termios orig) {
    struct termios ts = orig;

    ts.c_lflag &= ~(ICANON | ECHO);
    ts.c_lflag |= ISIG;
    ts.c_iflag &= ~ICRNL;

    ts.c_cc[VMIN] = 1;
    ts.c_cc[VTIME] = 0;

    return ts;
}

TEST_SETUP(termios) {
    test_setup();
}

TEST_TEAR_DOWN(termios) {
    test_teardown();
}

/************ Tests ************/

// NOLINTBEGIN(bugprone-suspicious-missing-comma)

TEST(termios, printty_cbreak_termios_value) {
    assert_termios(printty_cbreak_tio(tio));
}

TEST(termios, initial_shell_termios) {
    vt_send_str("sleep 100\n");
    assert_termios(tio);
}

TEST(termios, cbreak_mode_restored_after_fg_job) {
    vt_send_str("echo x\n");
    assert_term(2, PROMPT_LEN,
        PROMPT "echo x",
        "x",
        PROMPT
    );

    assert_termios(printty_cbreak_tio(tio));
}

TEST(termios, stopped_job_termios_saved_and_restored) {
    struct termios changed_tio = tio;
    changed_tio.c_lflag &= ~ECHO;

    vt_send_str("change_tio 100\n");
    assert_term(2, 0,
        PROMPT "change_tio 100",
        "x"
    );
    assert_termios(changed_tio);

    vt_send_ch('z', VTERM_MOD_CTRL);
    assert_term_clear(4, PROMPT_LEN,
        PROMPT "change_tio 100",
        "x",
        "",
        "[1] Stopped          change_tio 100%s",
        PROMPT
    );
    assert_termios(printty_cbreak_tio(tio));

    vt_send_str("write_sleep\n");
    assert_term(2, 0,
        PROMPT "write_sleep",
        "x"
    );
    assert_termios(tio);

    vt_send_ch('c', VTERM_MOD_CTRL);
    assert_term_clear(3, PROMPT_LEN,
        PROMPT "write_sleep",
        "x",
        "^C%%",
        PROMPT
    );
    assert_termios(printty_cbreak_tio(tio));

    vt_send_str("fg %1\n");
    assert_term(2, 0,
        PROMPT "fg %%1",
        "[1] running          change_tio 100%s"
    );
    assert_termios(changed_tio);

    vt_send_ch('c', VTERM_MOD_CTRL);
    assert_term_clear(3, PROMPT_LEN,
        PROMPT "fg %%1",
        "[1] running          change_tio 100%s",
        "",
        PROMPT
    );
    assert_termios(printty_cbreak_tio(changed_tio));

    vt_send_str("write_sleep\n");
    assert_term(2, 0,
        PROMPT "write_sleep",
        "x"
    );
    assert_termios(changed_tio);
}

TEST(termios, program_after_fg_builtin_has_correct_termios) {
    vt_send_str("sleep 100 &\n");
    assert_term_clear(2, PROMPT_LEN,
        PROMPT "sleep 100 &",
        "[1] %d",
        PROMPT
    );

    vt_send_str("fg %1 || cat\n");
    assert_term(1, 0,
        PROMPT "fg %%1 || cat"
    );

    assert_kill(SIGTERM, "sleep");

    assert_term(2, 0,
        PROMPT "fg %%1 || cat",
        "[1] Terminated       sleep 100%s"
    );

    assert_pstate("cat", PSTATE_SLEEPING);
    assert_termios(tio);

    vt_send_str("abc\n");
    assert_term(4, 0,
        PROMPT "fg %%1 || cat",
        "[1] Terminated       sleep 100%s",
        "abc",
        "abc"
    );
}

// NOLINTEND(bugprone-suspicious-missing-comma)

/************ Test runner ************/

TEST_GROUP_RUNNER(termios) {
    init_tio();
    RUN_TEST_CASE(termios, printty_cbreak_termios_value);
    RUN_TEST_CASE(termios, initial_shell_termios);
    RUN_TEST_CASE(termios, cbreak_mode_restored_after_fg_job);
    RUN_TEST_CASE(termios, stopped_job_termios_saved_and_restored);
    RUN_TEST_CASE(termios, program_after_fg_builtin_has_correct_termios);
}
