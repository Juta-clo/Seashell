#include "unity_fixture.h"
#include "pty_test.h"

TEST_GROUP(termios);

/************ Fixture ************/

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

void ms_sleep(int ms) {
    int sec = ms / 1000;
    ms %= 1000;
    int ns = ms * 1000000;
    struct timespec ts = { .tv_nsec = ns, .tv_sec = sec };
    if (nanosleep(&ts, NULL) == -1)
         TEST_FAIL_MESSAGE("nanosleep failed");
}

void assert_termios(struct termios exp) {
    struct termios tio;

    for (int i = 0; i < 100; ++i) {
        xtcgetattr(cur_test->mfd, &tio);
        if (memcmp(exp.c_cc, tio.c_cc, sizeof(tio.c_cc))            == 0 &&
            memcmp(&exp.c_iflag, &tio.c_iflag, sizeof(tio.c_iflag)) == 0 &&
            memcmp(&exp.c_oflag, &tio.c_oflag, sizeof(tio.c_oflag)) == 0 &&
            memcmp(&exp.c_cflag, &tio.c_cflag, sizeof(tio.c_cflag)) == 0 &&
            memcmp(&exp.c_lflag, &tio.c_lflag, sizeof(tio.c_lflag)) == 0)
        {
            return;
        }

        ms_sleep(1);
    }

    TEST_FAIL_MESSAGE("assert_termios timeout");
}

TEST_SETUP(termios) {
    new_shell();
}

TEST_TEAR_DOWN(termios) {
    delete_shell();
}

/************ Tests ************/

// NOLINTBEGIN(bugprone-suspicious-missing-comma)

TEST(termios, printty_cbreak_termios_value) {
    struct termios test_tio = printty_cbreak_tio(tio);
    assert_termios(test_tio);
}

TEST(termios, initial_shell_termios) {
    vterm_send_str("sleep 100\n");
    assert_termios(tio);
}

TEST(termios, cbreak_mode_restored_after_fg_job) {
    vterm_send_str("echo x\n");
    assert_term(
        PROMPT "echo",
        "x"
    );

    assert_termios(printty_cbreak_tio(tio));
}

TEST(termios, stopped_job_termios_saved_and_restored) {
    struct termios changed_tio = tio;
    changed_tio.c_lflag &= ~ECHO;

    vterm_send_str("change_tio 100\n");
    assert_term(
        PROMPT "change_tio 100",
        "x"
    );
    assert_termios(changed_tio);

    vterm_send_char('z', VTERM_MOD_CTRL);
    assert_term(
        PROMPT "change_tio 100",
        "x",
        " ",
        "[1] Stopped",
        PROMPT
    );
    assert_termios(printty_cbreak_tio(tio));

    vterm_send_str("write_sleep\n");
    assert_term(
        PROMPT "change_tio 100",
        "x",
        " ",
        "[1] Stopped",
        PROMPT "write_sleep",
        "x"
    );
    assert_termios(tio);

    vterm_send_char('c', VTERM_MOD_CTRL);
    assert_term(
        PROMPT "change_tio 100",
        "x",
        " ",
        "[1] Stopped",
        PROMPT "write_sleep",
        "x",
        "^C" PROMPT
    );
    assert_termios(printty_cbreak_tio(tio));

    vterm_send_str("fg %1\n");
    assert_term(
        PROMPT "change_tio 100",
        "x",
        " ",
        "[1] Stopped",
        PROMPT "write_sleep",
        "x",
        "^C" PROMPT "fg %1",
        "[1] Continued"
    );
    assert_termios(changed_tio);

    vterm_send_char('c', VTERM_MOD_CTRL);
    assert_term(
        PROMPT "change_tio 100",
        "x",
        " ",
        "[1] Stopped",
        PROMPT "write_sleep",
        "x",
        "^C" PROMPT "fg %1",
        "[1] Continued",
        " ",
        PROMPT
    );
    assert_termios(printty_cbreak_tio(changed_tio));

    vterm_send_str("write_sleep\n");
    assert_term(
        PROMPT "change_tio 100",
        "x",
        " ",
        "[1] Stopped",
        PROMPT "write_sleep",
        "x",
        "^C" PROMPT "fg %1",
        "[1] Continued",
        " ",
        PROMPT "write_sleep",
        "x"
    );
    assert_termios(changed_tio);
}

// NOLINTEND(bugprone-suspicious-missing-comma)

/************ Test runner ************/

TEST_GROUP_RUNNER(termios) {
    RUN_TEST_CASE(termios, printty_cbreak_termios_value);
    RUN_TEST_CASE(termios, initial_shell_termios);
    RUN_TEST_CASE(termios, cbreak_mode_restored_after_fg_job);
    RUN_TEST_CASE(termios, stopped_job_termios_saved_and_restored);
}
