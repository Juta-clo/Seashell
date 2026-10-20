#include <git2/errors.h>
#include <git2/patch.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

#include "tutils.h"
#include "linc_tools.h"
#include "pty_test.h"
#include "unity_fixture.h" // IWYU pragma: keep

void test_fatal(const char *file, int line, const char *fmt, ...) {
    va_list va;

    fflush(stdout);
    fflush(stderr);

    fprintf(stderr, "\n\x1b[41m!FATAL ERROR!\x1b[0m:%s:%d: ", file, line);
    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
    fprintf(stderr, "\n");

    test_harness_deinit();
    exit(EXIT_FAILURE);
}

void add_lines(da_dstr *lines, const char *str) {
    assert(lines && str);

    if (strcmp(str, "") == 0)
        return;

    if (lines->size == 0) {
        dstr *line = da_push(lines);
        dstr_init(line);
    }

    for (const char *c = str; *c != '\0'; ++c) {
        if (*c == '\n') {
            dstr *line = da_push(lines);
            dstr_init(line);
            continue;
        }

        dstr_push(&lines->data[lines->size - 1], *c);
    }
}

__attribute__ ((format(printf, 1, 2)))
void test_write(const char *fmt, ...) {
    va_list va;
    char *p;

    va_start(va, fmt);
    if (vasprintf(&p, fmt, va) < 0)
        xtest_fatal("vasprintf: %m");
    va_end(va);

    add_lines(&test->test_lines, p);
    add_lines(&test->test_lines, "\n");
    free(p);
}

void test_relink(const char *target, const char *link) {
    if (unlink(link) == -1 && errno != ENOENT)
        xtest_fatal("unlink(%s): %m", link);

    if (symlink(target, link) == -1)
        xtest_fatal("symlink(%s, %s): %m", target, link);
}

FILE *new_tmp_file(char *path) {
    int fd = mkstemp(path);
    if (fd == -1)
        xtest_fatal("mkstemp(%s): %m", path);

    FILE *f = fdopen(fd, "r+");
    if (!f)
        xtest_fatal("fdopen: %m");

    return f;
}

void convert_special_chars(char **output_msg) {
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

static int diff_line_cb([[maybe_unused]] const git_diff_delta *delta,
        [[maybe_unused]] const git_diff_hunk *hunk,
        const git_diff_line *line, void *payload)
{
    dstr *output = payload;

    if (line->origin != GIT_DIFF_LINE_ADDITION &&
        line->origin != GIT_DIFF_LINE_DELETION &&
        line->origin != GIT_DIFF_LINE_CONTEXT &&
        line->origin != GIT_DIFF_LINE_HUNK_HDR)
    {
        return 0;
    }

    if (line->origin != GIT_DIFF_LINE_HUNK_HDR)
        dstr_printf(output, "%c", line->origin);

    dstrncat(output, line->content, line->content_len);

    return 0;
}

void get_diff_msg(void) {
    git_patch *patch = NULL;
    dstr buf;
    dstr_init(&buf);

    dstr exp_str;
    dstr_init(&exp_str);
    for (size_t i = 0; i < test->vt_exp.lines.size; ++i)
        dstr_printf(&exp_str, "%s\n", test->vt_exp.lines.data[i]);

    dstr act_str;
    dstr_init(&act_str);
    for (size_t i = 0; i < test->vt_act.lines.size; ++i)
        dstr_printf(&act_str, "%s\n", test->vt_act.lines.data[i]);

    if (git_patch_from_buffers(&patch, exp_str.c_str, exp_str.len,
            NULL, act_str.c_str, act_str.len, NULL, NULL) != 0) {
        const git_error *err = git_error_last();
        if (err) {
            xtest_fatal("git_patch_from_buffers: %s", err->message);
        } else
            xtest_fatal("git_patch_from_buffers: (git_error_last failed)");
    }

    if (git_patch_print(patch, diff_line_cb, &buf) != 0) {
        const git_error *err = git_error_last();
        if (err) {
            xtest_fatal("git_patch_print: %s", err->message);
        } else
            xtest_fatal("git_patch_print: (git_error_last failed)");
    }

    if (buf.len > 0) {
        dstr_printf(&test->diff_msg, "--- expected\n+++ actual\n%s", buf.c_str);
    } else {
        dstr_printf(&test->diff_msg, "[no diff]\n");
    }

    git_patch_free(patch);
    dstr_free(&buf);
    dstr_free(&exp_str);
    dstr_free(&act_str);
}

struct itimerspec ms_to_its(int ms) {
    int sec = ms / 1000;
    ms %= 1000;

    int ns = ms * 1000000;

    struct itimerspec its = {
        .it_value = {.tv_sec = sec, .tv_nsec = ns},
        .it_interval = {0}
    };

    return its;
}

void set_timerfd(int tfd, int ms) {
    struct itimerspec its = ms_to_its(ms);
    if (timerfd_settime(tfd, 0, &its, NULL) == -1)
        xtest_fatal("timerfd_settime: %m");
}

int start_timerfd(int ms) {
    struct itimerspec its;
    its = ms_to_its(ms);

    int tfd = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);
    if (tfd == -1)
        xtest_fatal("timerfd_create: %m");

    if (timerfd_settime(tfd, 0, &its, NULL) == -1)
        xtest_fatal("timerfd_settime: %m");

    return tfd;
}

char *flush_stream(int fd, bool break_on_eio) {
#define FLUSH_STREAM_BS 4096
    int flags = fcntl(fd, F_GETFL);
    if (flags == -1)
        xtest_fatal("fcntl");

    if (!(flags & O_NONBLOCK))
        xtest_fatal("flush_stream receieved blocking fd");

    char *out = NULL;
    size_t out_size = 0;

    while (true) {
        char pbuf[FLUSH_STREAM_BS];
        int num_read = read(fd, pbuf, FLUSH_STREAM_BS);

        if (num_read == -1) {
            if (errno == EAGAIN) {
                break;
            } else if (break_on_eio && errno == EIO) {
                break;
            } else {
                xtest_fatal("read: %m");
            }
        }

        out = realloc(out, out_size + num_read);
        if (!out)
            xtest_fatal("realloc: %m");

        memcpy(out + out_size, pbuf, num_read);
        out_size += num_read;
    }

    out = realloc(out, out_size + 1);
    if (!out)
        xtest_fatal("realloc: %m");
    out[out_size] = '\0';

    return out;
}
