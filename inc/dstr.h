#ifndef DSTR_H
#define DSTR_H

#include <stddef.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct dstr {
    char *c_str;
    size_t len;
    size_t size;
    size_t cap;
};

typedef struct dstr dstr;

static inline void dstr_init(dstr *str) {
    assert(str);

    *str = (dstr){0};

    str->c_str = malloc(1);
    if (!str->c_str)
        exit(EXIT_FAILURE);

    str->c_str[0] = '\0';

    str->len = 0;
    str->size = 1;
    str->cap = 1;
}

static inline void dstr_free(dstr *str) {
    assert(str);

    free(str->c_str);
    *str = (dstr){0};
}

static inline void dstr_reserve(dstr *str, size_t min) {
    assert(str);

    if (min == 0 || min <= str->cap) /* no-op */
        return;

    void *tmp = realloc(str->c_str, min * sizeof(*str->c_str));
    if (!tmp)
        exit(EXIT_FAILURE);

    str->c_str = tmp;
    str->cap = min;
}

static inline void dstr_push(dstr *str, char c) {
    assert(str);

    dstr_reserve(str, str->size + 1);

    ++str->size;
    ++str->len;

    str->c_str[str->len - 1] = c;
    str->c_str[str->size - 1] = '\0';
}

static inline void dstr_clear(dstr *str) {
    assert(str);

    str->c_str[0] = '\0';
    str->len = 0;
    str->size = 1;
}

static inline void dstrcat(dstr *dst, const char *src) {
    assert(dst);
    assert(src);

    size_t src_len = strlen(src);

    dstr_reserve(dst, dst->size + src_len);
    dst->size += src_len;

    strncat(&dst->c_str[dst->len], src, src_len);
    dst->len += src_len;
}

__attribute__ ((format(printf, 2, 3)))
static inline void dstr_printf(dstr *dst, char *fmt, ...) {
    assert(dst && fmt);

    va_list va;
    char max_fmt_print[8192];

    va_start(va, fmt);
    if (vsnprintf(max_fmt_print, 8192, fmt, va) < 0)
        exit(EXIT_FAILURE);
    va_end(va);

    dstrcat(dst, max_fmt_print);
}

static inline void dstrcpy(dstr *dst, char *src) {
    assert(dst && src);

    size_t src_len = strlen(src);

    dstr_reserve(dst, dst->size + src_len);
    dst->size = src_len + 1;

    strncpy(dst->c_str, src, dst->size);
    dst->len = src_len;
}

static inline void dstrncpy(dstr *dst, char *src, size_t n) {
    assert(dst && src);

    dstr_reserve(dst, n + 1);
    dst->size = n + 1;

    strncpy(dst->c_str, src, n);
    dst->c_str[n] = '\0';

    dst->len = n;
}

static inline void dstrncat(dstr *dst, const char *src, size_t n) {
    assert(dst && src);

    dstr_reserve(dst, dst->size + n);
    dst->size += n;

    strncat(&dst->c_str[dst->len], src, n);
    dst->len += n;
}

#endif
