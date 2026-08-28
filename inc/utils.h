#ifndef UTILS_H
#define UTILS_H

#include <stdnoreturn.h>

#include "llog.h"

#define UTIL_PFFORMAT(x, y) __attribute__ ((__format__(printf, (x), (y))))
#define UTIL_NORETURN __attribute__ ((__noreturn__))

UTIL_PFFORMAT(1, 2) UTIL_NORETURN
noreturn void fatal(const char *fmt, ...);

UTIL_PFFORMAT(1, 2) UTIL_NORETURN
void err_exit(const char *fmt, ...);

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
