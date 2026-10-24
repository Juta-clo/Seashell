#ifndef T_UTILS_H
#define T_UTILS_H

#include <stdio.h>
#include <vterm.h>

#include <linc/array.h>
#include <linc/string.h>

#define xtest_fatal(fmt, ...) \
    test_fatal(__FILE__, __LINE__, fmt __VA_OPT__(,) __VA_ARGS__)

#define MAX(a, b) \
    ((a) > (b)) ? a : b

#define MIN(a, b) \
    ((a) < (b)) ? a : b

#define ARR_LEN(arr) \
    (sizeof(arr) / sizeof(typeof(*arr)))

#define flush_pty(_fd) \
    flush_stream(_fd, true)
#define flush_pipe(_fd) \
    flush_stream(_fd, false)

LINC_ARRAY_TYPE(da_dstr, linc_string);
LINC_ARRAY_TYPE(da_int, int);

__attribute__ ((format(printf, 3, 4)))
void test_fatal(const char *file, int line, const char *fmt, ...);

void test_write(const char *fmt, ...);
[[nodiscard]] FILE *new_tmp_file(char *path);
void test_relink(const char *target, const char *link);
void convert_special_chars(linc_string *str_out);

struct itimerspec ms_to_its(int ms);
void set_timerfd(int tfd, int ms);
int start_timerfd(int ms);

char *flush_stream(int fd, bool break_on_eio);
void add_lines(da_dstr *lines, const char *str);

#endif
