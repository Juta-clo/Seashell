#ifndef UTILS_H
#define UTILS_H

#include <stdnoreturn.h>
#include <linc.h>

#include "llog.h"
#include "darr.h"

#define UTIL_PFFORMAT(x, y) __attribute__ ((__format__(printf, (x), (y))))
#define UTIL_NORETURN __attribute__ ((__noreturn__))

UTIL_PFFORMAT(1, 2) UTIL_NORETURN
noreturn void fatal(const char *fmt, ...);

UTIL_PFFORMAT(1, 2) UTIL_NORETURN
void err_exit(const char *fmt, ...);

#define ASSERT_EQ_INT(a, b) \
    err_assert_eq_int(a, b, #a, #b, __FILE__, __LINE__);
void err_assert_eq_int(int a, int b, const char *p1, const char *p2,
        const char *file, int line);

#define ASSERT_NEQ_INT(a, b) \
    err_assert_not_eq_int(a, b, #a, #b, __FILE__, __LINE__);
void err_assert_not_eq_int(int a, int b, const char *p1, const char *p2,
        const char *file, int line);

#define ASSERT_G_INT(a, b) \
    err_assert_g_int(a, b, #a, #b, __FILE__, __LINE__);
void err_assert_g_int(int a, int b, const char *p1, const char *p2,
        const char *file, int line);

#define ASSERT_GE_INT(a, b) \
    err_assert_ge_int(a, b, #a, #b, __FILE__, __LINE__);
void err_assert_ge_int(int a, int b, const char *p1, const char *p2,
        const char *file, int line);

#define ASSERT(ass) \
    err_assert(ass, #ass, __FILE__, __LINE__);
void err_assert(bool b, const char *text, const char *file, int line);

void err_msg(const char *fmt, ...);

void errno_msg(const char *fmt, ...);

UTIL_PFFORMAT(1, 2) UTIL_NORETURN
void usage_err(const char *fmt, ...);

DA_TYPE(da_charp, char *);

#define MAX(a, b) ((a) > (b)) ? (a) : (b)
#define MIN(a, b) ((a) < (b)) ? (a) : (b)

bool is_dir(const char *file);
size_t argv_len(char **argv);
void free_charp_arr(da_charp *arr);
void free_argv(char **argv);
[[nodiscard]] char **copy_argv(char **argv);

#define xfatal(fmt, ...) \
    do { \
        LOG_ERR(fmt __VA_OPT__(,) __VA_ARGS__); \
        fatal(fmt __VA_OPT__(,) __VA_ARGS__); \
    } while (false)

#define xprintty_cursor_move(pt, row, col) \
    do { if (printty_cursor_move(pt, row, col) == PRINTTY_ERR) \
            xfatal("printty_cursor_move"); } while (false)

#define xprintty_write(pt, uc) \
    do { if (printty_write(pt, uc) == PRINTTY_ERR) \
            xfatal("printty_write"); } while (false)

#define xprintty_write_name(pt, name) \
    do { if (printty_write_name(pt, name) == PRINTTY_ERR) \
            xfatal("printty_write_name"); } while (false)

#define xprintty_get_cursor_pos(pt, row, col) \
    do { if (printty_get_cursor_pos(pt, row, col) == PRINTTY_ERR) \
            xfatal("printty_get_cursor_pos"); } while (false)

#define xprintty_get_term_size(pt, rows, cols) \
    do { if (printty_get_term_size(pt, rows, cols) == PRINTTY_ERR) \
            xfatal("printty_get_term_size"); } while (false)

#define xprintty_set_color(pt, fg, bg) \
    do { if (printty_set_color(pt, fg, bg) == PRINTTY_ERR) \
            xfatal("printty_set_color"); } while (false)

#define xprintty_cstyle(pt, style) \
    do { if (printty_cstyle(pt, style) == PRINTTY_ERR) \
            xfatal("printty_cstyle"); } while (false)

#endif
