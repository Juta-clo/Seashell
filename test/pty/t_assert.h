#ifndef T_ASSERT_H
#define T_ASSERT_H

#include <termios.h>

#include <linc/array.h>

#include "t_fixture.h" // IWYU pragma: keep
#include "t_vterm.h" // IWYU pragma: keep
#include "t_harness.h" // IWYU pragma: keep

#define PSTATE_ZOMBIE   "Z"
#define PSTATE_RUNNING  "R"
#define PSTATE_SLEEPING "S"
#define PSTATE_STOPPED  "T"

#define PROMPT "> "
#define PROMPT_LEN 2

#define HL_COL(_col, _str) "%[" #_col "]" #_str "%[]"
#define HL_COL_RAW(_col, _str) "%[" _col "]" _str "%[]"

#define HL_CMD(_cmd) HL_COL(g, _cmd)
#define HL_CMD_RAW(_cmd) HL_COL_RAW("g", _cmd)
#define HL_STR(_str) HL_COL_RAW("G", _str)
#define HL_STR_PATH(_str) HL_COL_RAW("G,,u", _str)

#define HL_OPT(_opt) HL_COL(c, _opt)
#define HL_OPT_LONG(_opt) HL_COL(x, _opt)

#define HL_IO(_io) HL_COL(c, _io)
#define HL_RDR_OUT HL_COL(c, >)
#define HL_RDR_IN HL_COL(c, <)
#define HL_APPEND HL_COL(c, >>)
#define HL_FD_OUT HL_COL(c, >&)
#define HL_FD_IN HL_COL(c, <&)

#define HL_PIPE HL_COL(y, |)
#define HL_AND_IF HL_COL(y, &&)
#define HL_OR_IF HL_COL(y, ||)
#define HL_AMPER HL_COL_RAW("c,,b", "&")

#define HL_LPAREN HL_COL_RAW("m", "(")
#define HL_RPAREN HL_COL_RAW("m", ")")

#define HL_PATH(_path) HL_COL_RAW(",,u", #_path)

#define HL_INVAL "%[b,r,b]?%[]"
#define HL_SYNTAX(_str) HL_COL_RAW("r,,b", #_str)
#define HL_NO_CMD(_cmd) HL_COL_RAW("r,,b", #_cmd)
#define HL_NO_CMD_RAW(_cmd) HL_COL_RAW("r,,b", _cmd)

#define HL_SHADOW(_str) HL_COL_RAW("B", #_str)
#define HL_SHADOW_RAW(_str) HL_COL_RAW("B", _str)

bool verify_bytes(const char *str);
bool verify_kill(int sig, const char *name);
bool verify_pstate(const char *name, const char *state);
bool verify_termios(struct termios exp);
bool verify_term(char **lines, int crow, int ccol, bool attr);

#define PTY_TEST_FAIL(fmt, ...) \
    do { \
        test_push_err(fmt __VA_OPT__(,) __VA_ARGS__); \
        test->fail_line = __LINE__; \
        TEST_FAIL(); \
    } while (false)

#define LN(...) (char *[]){ __VA_ARGS__, NULL }

#define assert_term(_row, _col, ...) \
    do { \
        if (!verify_term(LN(__VA_ARGS__), _row, _col, false)) \
            PTY_TEST_FAIL("verify_term() timed out"); \
        vt_take_snap(); \
    } while (false)

#define assert_term_attr(_row, _col, ...) \
    do { \
        if (!verify_term(LN(__VA_ARGS__), _row, _col, true)) \
            PTY_TEST_FAIL("verify_term() timed out"); \
        vt_take_snap(); \
    } while (false)

#define assert_term_attr_clear(_row, _col, ...) \
    do { \
        if (!verify_term(LN(__VA_ARGS__), _row, _col, true)) \
            PTY_TEST_FAIL("verify_term() timed out"); \
        vt_take_snap(); \
        vt_send_ch('l', VTERM_MOD_CTRL); \
    } while (false)

#define assert_term_clear(_row, _col, ...) \
    do { \
        if (!verify_term(LN(__VA_ARGS__), _row, _col, false)) \
            PTY_TEST_FAIL("verify_term() timed out"); \
        vt_take_snap(); \
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

#endif
