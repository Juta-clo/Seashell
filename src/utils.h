#ifndef UTILS_H
#define UTILS_H

#include <stdnoreturn.h>

#include "linc.h"
#include "linc/array.h"
#include "linc/log.h" // IWYU pragma: export
#include "state.h" // IWYU pragma: export

#define LOG_INFO(...) \
    linc_log_write(env.shell_log, LINC_LOG_INFO, LINC_SITE, __VA_ARGS__)

#define LOG_WARN(...) \
    linc_log_write(env.shell_log, LINC_LOG_WARN, LINC_SITE, __VA_ARGS__)

#define LOG_ERR(...) \
    linc_log_write(env.shell_log, LINC_LOG_ERROR, LINC_SITE, __VA_ARGS__)

#define MIN(a, b) \
    ({ \
        __auto_type min_a = (a); \
        __auto_type min_b = (b); \
        \
        ((min_a) < (min_b)) ? (min_a) : (min_b); \
    })

#define MAX(a, b) \
    ({ \
        __auto_type max_a = (a); \
        __auto_type max_b = (b); \
        \
        ((max_a) > (max_b)) ? (max_a) : (max_b); \
    })

#define STREQ(_a, _b) (strcmp(_a, _b) == 0)

#define STRINGIFY(_str) #_str

#define ARRLEN(_arr) \
        sizeof(_arr) / sizeof(typeof(*_arr))

#define fatal(...) \
    do { \
        LOG_ERR(__VA_ARGS__); \
        fatal_imp(__VA_ARGS__); \
    } while (false)

__attribute__ ((__format__(printf, 1, 2), __noreturn__))
void fatal_imp(const char *fmt, ...);

#define err_exit(...) \
    do { \
        LOG_ERR(__VA_ARGS__); \
        err_exit_imp(__VA_ARGS__); \
    } while (false)

__attribute__ ((__format__(printf, 1, 2), __noreturn__))
void err_exit_imp(const char *fmt, ...);

__attribute__ ((__format__(printf, 1, 2)))
void err_msg(const char *fmt, ...);

#define ASSERT_OP_EQ  ==
#define ASSERT_OP_NEQ !=
#define ASSERT_OP_G   >
#define ASSERT_OP_GEQ >=
#define ASSERT_OP_L   <
#define ASSERT_OP_LEQ <=

#define assert_imp(_op, _a, _b, _ap, _bp) \
    ({ \
        if (!(_a _op _b)) { \
            linc_log_write( \
                env.assert_log, LINC_LOG_ERROR, LINC_SITE, \
                "%s %s %s, %s = %Lg, %s = %Lg", \
                _ap, STRINGIFY(_op), _bp, \
                _ap, (long double)_a, _bp, (long double)_b \
            ); \
        \
            fatal_imp(nullptr); \
        } \
    })

#define ASSERT_(_op, _a, _b) \
    ({ \
        __auto_type assert_value_a = (_a); \
        __auto_type assert_value_b = (_b); \
        \
        assert_imp( \
            ASSERT_OP_##_op, \
            assert_value_a, assert_value_b, \
            #_a, #_b \
        ); \
    })

#define ASSERT(_a) \
    assert_bool_imp(LINC_SITE, _a, #_a);

void assert_bool_imp(linc_site site, ...);

LINC_ARRAY_TYPE(da_charp, char *);
__attribute__ ((__warn_unused_result__))
char **argv_copy(char **argv);
size_t argv_len(char **argv);
void argv_free(char **argv);
void argv_free_free(da_charp *arr);

void xdup2_move(int fd1, int fd2);
int get_int(const char *arg, int *num);
bool is_file(const char *path);
bool is_dir(const char *file);
char *strdup_alloc(const char *str, linc_allo *allo);
char *strndup_alloc(const char *str, size_t n, linc_allo *allo);

#endif
