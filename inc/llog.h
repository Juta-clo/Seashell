#ifndef LLOG_H
#define LLOG_H

#include <assert.h>
#include <execinfo.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stddef.h>
#include <unistd.h>

#include "dstr.h"

#define _LLOG_BT_SIZE 64

#define LLOG_SITE \
    (llog_site_info){ \
        .file = __FILE__, \
        .func = __func__, \
        .line = __LINE__, \
    }

/* ONLY USE THE LOG_ MACROS and llog_default() IF YOU
 * WILL NOT BE INCLUDED INTO ANOTHER PROJECT THAT MAY
 * ALSO USE THE LOG_ MACORS and llog_default.
 *
 * LIBRARIES SHOULD USE llog_log() EXPLICITLY */

#define LLOG_DEFAULT(log) \
    llog *llog_default(void) { \
        return log; \
    }

#define LOG_INFO(fmt, ...) \
    llog_log(llog_default(), LLOG_INFO, &LLOG_SITE, fmt __VA_OPT__(,) __VA_ARGS__)

#define LOG_WARN(fmt, ...) \
    llog_log(llog_default(), LLOG_WARN, &LLOG_SITE, fmt __VA_OPT__(,) __VA_ARGS__)

#define LOG_ERR(fmt, ...) \
    llog_log(llog_default(), LLOG_ERR, &LLOG_SITE, fmt __VA_OPT__(,) __VA_ARGS__)

#define LLOG_INFO(llog, fmt, ...) \
    llog_log(llog, LLOG_INFO, &LLOG_SITE, fmt __VA_OPT__(,) __VA_ARGS__)

#define LLOG_WARN(llog, fmt, ...) \
    llog_log(llog, LLOG_WARN, &LLOG_SITE, fmt __VA_OPT__(,) __VA_ARGS__)

#define LLOG_ERR(llog, fmt, ...) \
    llog_log(llog, LLOG_ERR, &LLOG_SITE, fmt __VA_OPT__(,) __VA_ARGS__)

typedef enum {
    LLOG_INFO,
    LLOG_WARN,
    LLOG_ERR,
} llog_lvl;

typedef struct {
    const char *file;
    const char *func;
    int line;
} llog_site_info;

typedef struct {
    int saved_errno;
    const llog_site_info *site;
    llog_lvl log_level;
    pid_t pid;
    pid_t ppid;
    pid_t pgid;
    pid_t tid;
    char *msg;
    void *bt[_LLOG_BT_SIZE];
    size_t bt_n;
} llog_info;

typedef void (llog_sink)(const llog_info *);

typedef struct {
    llog_sink *sink;
} llog;

llog *llog_default(void);

static inline void _llog_convert_special_chars(char **output_msg) {
    dstr out;
    dstr_init(&out);

    for (char *c = *output_msg; *c != '\0'; ++c) {
        switch (*c) {
        case '\n': dstrcat(&out, "\\n"); break;
        case '\t': dstrcat(&out, "\\t"); break;
        case '\r': dstrcat(&out, "\\r"); break;
        case '\b': dstrcat(&out, "\\b"); break;
        case '\a': dstrcat(&out, "\\a"); break;
        case '\f': dstrcat(&out, "\\f"); break;
        case '\v': dstrcat(&out, "\\v"); break;
        default:
            if (*c < 0x20) { /* fallback to caret notation */
                dstr_push(&out, '^');
                dstr_push(&out, *c + '@');
            } else if (*c == 0x7F) { /*DEL*/
                dstr_push(&out, *c);
            } else {
                dstr_push(&out, *c);
            }
        }
    }

    free(*output_msg);
    *output_msg = out.c_str;
}

__attribute__ ((__format__(printf, 4, 5)))
static inline void llog_log(llog *log, llog_lvl lvl, const llog_site_info *site,
        const char *fmt, ...)
{
    assert(log);
    assert(site);
    assert(fmt);

    if (!log->sink)
        return;

    int saved_errno = errno;

    llog_info info = {0};
    va_list va;

    info.saved_errno = saved_errno;
    info.site = site;
    info.log_level = lvl;
    info.pid = getpid();
    info.ppid = getppid();
    info.pgid = getpgrp();
    info.tid = gettid();
    info.bt_n = backtrace(info.bt, _LLOG_BT_SIZE);

    va_start(va, fmt);
     if (vasprintf(&info.msg, fmt, va) < 0)
         exit(EXIT_FAILURE);
     va_end(va);

    _llog_convert_special_chars(&info.msg);
    log->sink(&info);
    free(info.msg);

    errno = saved_errno;
    return;
}

void assert_llog_sink(const llog_info *info);
void shell_llog_sink(const llog_info *info);

#endif
