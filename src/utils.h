#ifndef UTILS_H
#define UTILS_H

#include <stdnoreturn.h>
#include <linc.h>

#include <linc/array.h>
#include "linc/log.h" // IWYU pragma: export
#include "state.h" // IWYU pragma: export

#define UTIL_PFFORMAT(x, y) __attribute__ ((__format__(printf, (x), (y))))
#define UTIL_NORETURN __attribute__ ((__noreturn__))

UTIL_PFFORMAT(1, 2) UTIL_NORETURN
noreturn void fatal(const char *fmt, ...);

#define err_exit(...) \
    do { \
        LOG_ERR(__VA_ARGS__); \
        err_exit_imp(__VA_ARGS__); \
    } while (false)

UTIL_PFFORMAT(1, 2) UTIL_NORETURN
void err_exit_imp(const char *fmt, ...);

#define ASSERT_EQ_INT(a, b) \
    err_assert_eq_int(a, b, #a, #b, LINC_SITE);
void err_assert_eq_int(int a, int b, const char *p1, const char *p2,
        linc_site site);

#define ASSERT_NEQ_INT(a, b) \
    err_assert_not_eq_int(a, b, #a, #b, LINC_SITE);
void err_assert_not_eq_int(int a, int b, const char *p1, const char *p2,
        linc_site site);

#define ASSERT_G_INT(a, b) \
    err_assert_g_int(a, b, #a, #b, LINC_SITE);
void err_assert_g_int(int a, int b, const char *p1, const char *p2,
        linc_site site);

#define ASSERT_GE_INT(a, b) \
    err_assert_ge_int(a, b, #a, #b, LINC_SITE);
void err_assert_ge_int(int a, int b, const char *p1, const char *p2,
        linc_site site);

#define ASSERT(ass) \
    err_assert(ass, #ass, LINC_SITE);
void err_assert(bool b, const char *text, linc_site site);

void err_msg(const char *fmt, ...);

void errno_msg(const char *fmt, ...);

UTIL_PFFORMAT(1, 2) UTIL_NORETURN
void usage_err(const char *fmt, ...);

LINC_ARRAY_TYPE(da_charp, char *);
LINC_ARRAY_TYPE(da_int, int);

#define MAX(a, b) ((a) > (b)) ? (a) : (b)
#define MIN(a, b) ((a) < (b)) ? (a) : (b)

int get_int(const char *arg);
bool is_dir(const char *file);
size_t argv_len(char **argv);
void free_charp_arr(da_charp *arr);
void free_argv(char **argv);
[[nodiscard]] char **copy_argv(char **argv);

char *strdup_alloc(const char *str, linc_allo *allo);

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

#define LOG_INFO(...) \
    linc_log_write(sh_env.shell_log, LINC_LOG_INFO, LINC_SITE, __VA_ARGS__)

#define LOG_WARN(...) \
    linc_log_write(sh_env.shell_log, LINC_LOG_WARN, LINC_SITE, __VA_ARGS__)

#define LOG_ERR(...) \
    linc_log_write(sh_env.shell_log, LINC_LOG_ERROR, LINC_SITE, __VA_ARGS__)

#define LLOG_INFO(_llog, ...) \
    linc_log_write(_llog, LLOG_INFO, LINC_SITE, __VA_ARGS__)

#define LLOG_WARN(_llog, ...) \
    linc_log_write(_llog, LLOG_WARN, LINC_SITE, __VA_ARGS__)

#define LLOG_ERR(_llog, ...) \
    linc_log_write(_llog, LLOG_ERR, LINC_SITE, __VA_ARGS__)

#endif
