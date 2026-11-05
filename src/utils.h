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

#define assert_imp(_site, _type, _a, _b, _ap, _bp) \
    ({ \
        bool result; \
        char *op; \
        \
        switch (_type) { \
        case ASSERT_EQ:  result = (_a == _b); op = "=="; break; \
        case ASSERT_NEQ: result = (_a != _b); op = "!="; break; \
        case ASSERT_G:   result = (_a > _b);  op = ">";  break; \
        case ASSERT_GEQ: result = (_a >= _b); op = ">="; break; \
        case ASSERT_L:   result =  (_a < _b);  op = "<";  break; \
        case ASSERT_LEQ: result = (_a <= _b); op = "<="; break; \
        default: \
            fatal("bad assertion type"); \
        } \
        \
        if (!result) { \
            linc_log_write( \
                env.assert_log, LINC_LOG_ERROR, _site, \
                "%s %s %s, %s = %Lg, %s = %Lg", \
                _ap, op, _bp, _ap, (long double)_a, _bp, (long double)_b \
            ); \
        \
            fatal_imp(nullptr); \
        } \
    })

#define ASSERT_(_type, _a, _b) \
    ({ \
        assert_op assert_kind = (ASSERT_##_type); \
        __auto_type assert_a = (_a); \
        __auto_type assert_b = (_b); \
        \
        assert_imp(LINC_SITE, assert_kind, assert_a, assert_b, #_a, #_b); \
    })

#define ASSERT(_a) \
    assert_bool_imp(LINC_SITE, _a, #_a);

typedef enum {
    ASSERT_BOOL,
    ASSERT_EQ,
    ASSERT_NEQ,
    ASSERT_G,
    ASSERT_GEQ,
    ASSERT_L,
    ASSERT_LEQ,
} assert_op;

void assert_bool_imp(linc_site site, ...);

LINC_ARRAY_TYPE(da_charp, char *);
__attribute__ ((__warn_unused_result__))
char **argv_copy(char **argv);
size_t argv_len(char **argv);
void argv_free(char **argv);
void argv_free_free(da_charp *arr);

void move_fd(int fd1, int fd2);
int get_int(const char *arg, int *num);
bool is_dir(const char *file);
char *strdup_alloc(const char *str, linc_allo *allo);

#endif
