#ifndef UTILS_H
#define UTILS_H

#include <stdnoreturn.h>

#include "linc_tools.h"

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

void free_argv(char **argv);
char **copy_argv(char **argv);

#define xfatal(fmt, ...) \
    do { \
        LOG_ERR(fmt __VA_OPT__(,) __VA_ARGS__); \
        fatal(fmt __VA_OPT__(,) __VA_ARGS__); \
    } while (false)

#endif
