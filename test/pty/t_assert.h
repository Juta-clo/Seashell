#ifndef T_ASSERT_H
#define T_ASSERT_H

#include <termios.h>

#include <linc/array.h>

#define PSTATE_ZOMBIE   "Z"
#define PSTATE_RUNNING  "R"
#define PSTATE_SLEEPING "S"
#define PSTATE_STOPPED  "T"

#define PROMPT "> "
#define PROMPT_LEN 2

#define PTY_TEST_FAIL(fmt, ...) \
    do { \
        test_push_err(fmt __VA_OPT__(,) __VA_ARGS__); \
        test->fail_line = __LINE__; \
        TEST_FAIL(); \
    } while (false)

#define LN(...) (char *[]){ __VA_ARGS__, NULL }

#define assert_term(_row, _col, ...) \
    do { \
        if (!verify_term(LN(__VA_ARGS__), _row, _col)) \
            PTY_TEST_FAIL("verify_term() timed out"); \
        *linc_array_push(&test->snaps) = vt_get_snap(); \
    } while (false)

#define assert_term_clear(_row, _col, ...) \
    do { \
        if (!verify_term(LN(__VA_ARGS__), _row, _col)) \
            PTY_TEST_FAIL("verify_term() timed out"); \
        *linc_array_push(&test->snaps) = vt_get_snap(); \
        vt_send_ch('l', VTERM_MOD_CTRL); \
    } while (false)

#define assert_bytes(str) \
    do { \
        if (!verify_bytes(str)) \
            PTY_TEST_FAIL("verify_bytes: failed to find " #str); \
    } while (false)

#define assert_kill(_sig, _name) \
    do { \
        if (!verify_kill(_sig, _name)) \
            PTY_TEST_FAIL("verify_kill: failed to kill " #_name); \
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

bool verify_bytes(const char *str);
bool verify_kill(int sig, const char *name);
bool verify_pstate(const char *name, const char *state);
bool verify_termios(struct termios exp);
bool verify_term(char **lines, int crow, int ccol);

#endif
