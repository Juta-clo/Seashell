#ifndef PTY_TEST_H
#define PTY_TEST_H

#include <ncurses.h>
#include <vterm.h>

#include "tutils.h"
#include "vterm_utils.h"

#define PROMPT "> "
#define PROMPT_LEN 2
#define PTY_RBUF 100000

#define PSTATE_ZOMBIE   "Z"
#define PSTATE_RUNNING  "R"
#define PSTATE_SLEEPING "S"
#define PSTATE_STOPPED  "T"

#define PTY_POLL_TFD   1
#define PTY_POLL_MFD   2

#define PTY_TEST_FAIL(fmt, ...) \
    do { \
        dstr_printf(&test->fail_msg, fmt __VA_OPT__(,) __VA_ARGS__); \
        test->fail_line = __LINE__; \
        TEST_FAIL_MESSAGE(test->fail_msg.c_str); \
    } while (false)

#define LN(...) (char *[]){ __VA_ARGS__, NULL }

#define assert_term(_row, _col, ...) \
    do { \
        if (!verify_term(LN(__VA_ARGS__), _row, _col)) \
            PTY_TEST_FAIL("verify_term: timed out"); \
        *da_push(&test->snaps) = vt_get_snap(); \
    } while (false)

#define assert_term_clear(_row, _col, ...) \
    do { \
        if (!verify_term(LN(__VA_ARGS__), _row, _col)) \
            PTY_TEST_FAIL("verify_term: timed out"); \
        *da_push(&test->snaps) = vt_get_snap(); \
        vt_send_ch('l', VTERM_MOD_CTRL); \
    } while (false)

#define assert_bytes(str) \
    do { \
        if (!verify_bytes(str)) \
            PTY_TEST_FAIL("verify_bytes: failed to find " #str); \
    } while (false)

#define assert_kill(name) \
    do { \
        if (!verify_kill(name)) \
            PTY_TEST_FAIL("verify_kill: failed to kill " #name); \
    } while (false)

#define assert_pstate(name, state) \
    do { \
        if (!verify_pstate(name, state)) \
            PTY_TEST_FAIL("verify_pstate: " #name " not in " #state); \
    } while (false)

#define assert_termios(exp) \
    do { \
        if (!verify_termios(exp)) \
            PTY_TEST_FAIL("verify_termios: timed out"); \
    } while (false)

typedef struct {
    int mfd;
    int cpid;
    int fail_line;
    double duration;
    bool test_ok;
    bool val_err;

    dstr pty_out_data;
    dstr pty_in_data;
    dstr fail_msg;
    dstr exit_msg;
    dstr diff_msg;

    vt_vterm vt;
    vt_snap vt_exp, vt_act;
    da_vt_snap snaps;

    char *test_name;
    char *test_file;

    da_dstr raw_lines;
    da_dstr shlog_lines;
    da_dstr val_lines;
    da_dstr test_lines;
} pty_test;

DA_TYPE(da_pty_test, pty_test);

typedef struct pty_harness {
    da_pty_test tests;
    int val_errs;
    int fails;
    char *cmd_str;
} pty_harness;

extern bool verbose;
extern bool memcheck;
extern bool script;

extern pty_harness ptyh;
extern pty_test *test;

extern struct termios tio;

void test_harness_init(int argc, const char *const *argv);
void test_harness_deinit(void);

void test_setup(void);
void test_teardown(void);
void test_resize_term(int row, int col);

bool verify_term(char **lines, int row, int col);
bool verify_bytes(const char *str);
bool verify_kill(const char *name);
bool verify_pstate(const char *name, const char *state);
bool verify_termios(struct termios exp);

#endif
