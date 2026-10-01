/*
 * linc_tools.h
 *
 * GENERATED FILE - DO NOT EDIT DIRECTLY
 *
 * Commit [76475ba]
 */

#ifndef LINC_TOOLS_H_INCLUDED
#define LINC_TOOLS_H_INCLUDED

/*------------------------------------
common.h
------------------------------------*/

#ifndef LINC_TOOLS_COMMON_H
#define LINC_TOOLS_COMMON_H

#include <stdlib.h>
#include <unistd.h>

#define _LINC_LIB_FATAL(msg) \
    _linc_lib_fatal("" msg "\n", sizeof("" msg "\n") - 1)

__attribute__ ((__noreturn__))
static inline void _linc_lib_fatal(const char *msg, int len) {
    write(STDERR_FILENO, msg, len);
    abort();
}

#endif

/*------------------------------------
hash.h
------------------------------------*/

#ifndef LINC_TOOLS_HASH_H
#define LINC_TOOLS_HASH_H

#include <stddef.h>
#include <stdint.h>

static inline uint32_t hash_str(const char *s, size_t n) {
    uint32_t val = 0;

    for (size_t i = 0; i < n; ++i)
        val = val * 31 + (uint8_t)s[i];

    return val;
}

#endif

/*------------------------------------
hda.h
------------------------------------*/

#ifndef LINC_TOOLS_HDA_H
#define LINC_TOOLS_HDA_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#define _HDA_MAX(a, b) \
    (((a) > (b)) ? a : b)

#define _HDA_BYTE_N (2 * sizeof(size_t))

#define _HDA_START(arr) \
    ((size_t *)((char *)(arr) - _HDA_BYTE_N))

#define _HDA_SIZE(arr) \
    (_HDA_START(arr)[0])

#define _HDA_CAP(arr) \
    (_HDA_START(arr)[1])

#define _HDA_EL_SZ(hda) \
    sizeof(*(hda))

#define hda_push(hda, ...) \
    ({ \
        hda = _hda_grow(hda, _HDA_EL_SZ(hda), 1); \
        *hda_last(hda) = (typeof(*(hda)))__VA_ARGS__; \
        hda_last(hda); \
    })

#define hda_insert(hda, index, n, ...) \
    (hda = _hda_insert_imp( \
            hda, \
            (typeof(*hda)[])__VA_ARGS__, \
            _HDA_EL_SZ(hda), \
            index, \
            n \
    ))

#define hda_insert_buf(hda, index, n, ...) \
    (hda = _hda_insert_imp( \
            hda, \
            (__VA_ARGS__), \
            _HDA_EL_SZ(hda), \
            index, \
            n \
    ))

#define hda_delete(hda, index, n) \
    _hda_delete_imp(hda, _HDA_EL_SZ(hda), index, n)

#define hda_last(hda) \
    ((typeof(hda))_hda_last_imp(hda, _HDA_EL_SZ(hda)))

#define hda_free(hda) \
    do { \
        _hda_free_imp(hda); \
        hda = nullptr; \
    } while (false)

static inline size_t *_hda_start(const void *hda) {
    return (hda) ? _HDA_START(hda) : NULL;
}

static inline size_t hda_size(const void *hda) {
    return (hda) ? _HDA_SIZE(hda) : 0;
}

static inline size_t hda_cap(const void *hda) {
    return (hda) ? _HDA_CAP(hda) : 0;
}

static inline void _hda_free_imp(void *hda) {
    if (!hda)
        return;

    free(_HDA_START(hda));
}

static inline void *_hda_reserve(void *hda, size_t el_sz, size_t min) {
    size_t cap = hda_cap(hda);

    if (cap >= min)
        return hda;

    cap = _HDA_MAX(min, _HDA_MAX(2, cap * 2));

    void *tmp = realloc(_hda_start(hda), _HDA_BYTE_N + (cap * el_sz));
    if (!tmp)
        _LINC_LIB_FATAL("realloc: out of memory");

    hda = (char *)tmp + _HDA_BYTE_N;
    _HDA_CAP(hda) = cap;

    return hda;
}

static inline void *_hda_last_imp(void *hda, size_t el_sz) {
    if (hda_size(hda) == 0)
        return NULL;

    char *p = hda;
    return &p[(hda_size(hda) - 1) * el_sz];
}

static inline void *_hda_grow(void *hda, size_t el_sz, size_t n) {
    size_t size = hda_size(hda);

    hda = _hda_reserve(hda, el_sz, size + n);
    _HDA_SIZE(hda) = size + n;

    return hda;
}

static inline void *_hda_insert_imp(void *hda, const void *els,
        size_t el_sz, size_t idx, size_t n)
{
    assert(els);
    assert(el_sz > 0);
    assert(idx <= hda_size(hda));

    if (n == 0)
        return hda;

    if (n > SIZE_MAX / el_sz)
        _LINC_LIB_FATAL("allocation overflow");

    /* to guard against overlapping hda and el on realloc.
     * Can avoid the malloc on every insert by checking for
     * address overlap (uintptr_t) */
    void *el_copy = malloc(el_sz * n);
    if (!el_copy)
        _LINC_LIB_FATAL("malloc: out of memory");

    memcpy(el_copy, els, el_sz * n);

    size_t size = hda_size(hda);
    hda = _hda_grow(hda, el_sz, n);

    char *p = hda;
    memmove(&p[(idx + n) * el_sz], &p[idx * el_sz], (size - idx) * el_sz);
    memcpy(&p[idx * el_sz], el_copy, el_sz * n);

    free(el_copy);
    return hda;
}

static inline void _hda_delete_imp(void *hda, size_t el_sz, size_t index,
        size_t n)
{
    assert(el_sz > 0);
    assert(index + n <= hda_size(hda));

    if (n == 0)
        return;

    char *p = hda;
    size_t shift_n = _HDA_SIZE(hda) - (index + n);

    memmove(&p[index * el_sz], &p[(index + n) * el_sz], shift_n * el_sz);

    _HDA_SIZE(hda) -= n;
}

#undef _HDA_MAX
#undef _HDA_BYTE_N
#undef _HDA_START
#undef _HDA_SIZE
#undef _HDA_CAP

#endif

/*------------------------------------
hds.h
------------------------------------*/

#ifndef LINC_TOOLS_HDS_S
#define LINC_TOOLS_HDS_S

#include <string.h>
#include <stddef.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>


#define hds_append(hds, s) \
    (hds = _hds_append_imp(hds, s, strlen(s)))

#define hds_append_n(hds, s, n) \
    (hds = _hds_append_imp(hds, s, n))

#define hds_append_fmt(hds, fmt, ...) \
    (hds = _hds_append_fmt_imp(hds, fmt __VA_OPT__(,) __VA_ARGS__))

#define hds_copy(hds, s) \
    (hds = _hds_copy_imp(hds, s, strlen(s)))

#define hds_copy_n(hds, s, n) \
    (hds = _hds_copy_imp(hds, s, n))

#define hds_copy_fmt(hds, fmt, ...) \
    (hds = _hds_copy_fmt_imp(hds, fmt __VA_OPT__(,) __VA_ARGS__))

static inline size_t hds_len(const void *hds) {
    size_t size = hda_size(hds);
    return (size == 0) ? 0 : size - 1;
}

static inline void *_hds_append_imp(void *hds, const void *b, size_t n) {
    size_t size = hda_size(hds);
    size_t idx = (size == 0) ? 0 : size - 1;

    hds = _hda_insert_imp(hds, b, sizeof(char), idx, n);

    if (size == 0) {
        hds = _hda_grow(hds, sizeof(char), 1);
        *(char *)_hda_last_imp(hds, 1) = '\0';
    }

    return hds;
}

__attribute__ ((__format__(printf, 2, 3)))
static inline void *_hds_append_fmt_imp(void *hds, const char *fmt, ...) {
    va_list va;
    char *s;

    va_start(va, fmt);
    int n = vasprintf(&s, fmt, va);
    va_end(va);

    if (n == -1)
        _LINC_LIB_FATAL("vasprintf: io error");

    hds_append_n(hds, s, n);

    free(s);

    return hds;
}

static inline void *_hds_copy_imp(char *hds, const char *s, size_t n) {
    hda_delete(hds, 0, hda_size(hds));
    hds_append_n(hds, s, n);

    return hds;
}

__attribute__ ((__format__(printf, 2, 3)))
static inline void *_hds_copy_fmt_imp(char *hds, const char *fmt, ...) {
    va_list va;
    char *s;

    va_start(va, fmt);
    int n = vasprintf(&s, fmt, va);
    va_end(va);

    if (n == -1)
        _LINC_LIB_FATAL("vasprintf: io error");

    hds_copy_n(hds, s, n);

    free(s);

    return hds;
}

static inline void hds_free(void *hds) {
    hda_free(hds);
}

#endif

/*------------------------------------
darr.h
------------------------------------*/

#ifndef LINC_TOOLS_DYN_ARR_H
#define LINC_TOOLS_DYN_ARR_H

#include <stdlib.h>
#include <string.h>


#define DA_MAX(a, b) \
    ((a) > (b)) ? (a) : (b)

#define DA_TYPE(name, type) \
    typedef struct { \
        type *data; \
        size_t size; \
        size_t cap; \
    } name;

static inline void *_da_reserve_imp(void *data, size_t el_sz, size_t min,
        size_t *cap)
{
    if (*cap >= min)
        return data;

    *cap = DA_MAX(min, DA_MAX(2, *cap * 2));

    void *tmp = realloc(data, *cap * el_sz);
    if (!tmp)
        _LINC_LIB_FATAL("realloc: out of memory");

    data = tmp;

    return data;
}

#define da_init(arr) \
    do { \
        assert(arr); \
        *(arr) = (typeof(*(arr))){0}; \
    } while (false)

#define da_free(arr) \
    do { \
        assert(arr); \
        free((arr)->data); \
        *(arr) = (typeof(*(arr))){0}; \
    } while (false)

#define da_reserve(arr, _min) \
    do { \
        assert(arr); \
        size_t min = _min; \
        (arr)->data = _da_reserve_imp( \
            (arr)->data, \
            sizeof(*(arr)->data), \
            min, \
            &(arr)->cap \
        ); \
    } while (false)

#define da_push(arr) \
    ({ \
        assert(arr); \
        da_reserve((arr), (arr)->size + 1); \
        &((arr)->data[(arr)->size++]); \
    })

#define da_delete(arr, index_) \
    do { \
        assert(arr); \
        size_t _da_i = (index_); \
        assert(_da_i < (arr)->size); \
        memmove( \
            &(arr)->data[_da_i], \
            &(arr)->data[_da_i + 1], \
            ((arr)->size - _da_i - 1) * sizeof(*(arr)->data) \
        ); \
        --(arr)->size; \
    } while (0)

#define da_insert(arr, index_) \
    ({ \
        assert(arr); \
        size_t _da_i = (index_); \
        assert(_da_i <= (arr)->size); \
        da_reserve((arr), (arr)->size + 1); \
        memmove( \
            &(arr)->data[_da_i + 1], \
            &(arr)->data[_da_i], \
            ((arr)->size - _da_i) * sizeof(*(arr)->data) \
        ); \
        ++(arr)->size; \
        &((arr)->data[_da_i]); \
    })

#define da_push_init(arr, init) \
    ({ \
        __auto_type _da_p = da_push(arr); \
        if (_da_p && (init)(_da_p) == -1) \
            _da_p = NULL; \
        _da_p; \
    })

#undef DA_MAX

#endif

/*------------------------------------
dstr.h
------------------------------------*/

#ifndef LINC_TOOLS_DSTR_H
#define LINC_TOOLS_DSTR_H

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
        _LINC_LIB_FATAL("malloc: out of memory");

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
        _LINC_LIB_FATAL("realloc: out of memory");

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
        _LINC_LIB_FATAL("vsnprintf: output error");
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

/*------------------------------------
trie.h
------------------------------------*/

#ifndef LINC_TOOLS_TRIE_H
#define LINC_TOOLS_TRIE_H

#include <stddef.h>
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>


#define _TRIE_CHILD_MAX 256

typedef enum {
    TRIE_MATCH,
    TRIE_AMBIG,
    TRIE_NONE,
    TRIE_WAIT,
} trie_status;

typedef struct Trie Trie;

struct Trie {
    Trie *children[_TRIE_CHILD_MAX];
    size_t child_n;
    bool term;
    void *data; /* valid if term is true */
};

#define trie_put(tr, s) \
    trie_add(tr, s, NULL, 0)

static inline Trie *trie_create(void) {
    Trie *tr = calloc(1, sizeof(Trie));
    if (!tr)
        _LINC_LIB_FATAL("calloc: out of memory");

    return tr;
}

static inline void trie_destroy(Trie *tr) {
    for (size_t i = 0; i < _TRIE_CHILD_MAX; ++i)
        if (tr->children[i])
            trie_destroy(tr->children[i]);

    if (tr->data)
        free(tr->data);

    free(tr);
}

static inline void trie_add(Trie *tr, const char *s, void *data, size_t sz) {
    assert(tr);
    assert(s);

    if (s[0] == '\0') {
        tr->term = true;

        if (sz > 0) {
            tr->data = malloc(sz);
            if (!tr->data)
                _LINC_LIB_FATAL("malloc: out of memory");

            memcpy(tr->data, data, sz);
        }

        return;
    }

    const uint8_t ch = s[0];

    if (!tr->children[ch]) {
        tr->children[ch] = trie_create();
        ++tr->child_n;
    }

    trie_add(tr->children[ch], &s[1], data, sz);
}

static inline void *trie_lookup(Trie *tr, const char *s, trie_status *stat) {
    assert(tr);
    assert(s);
    assert(stat);

    const uint8_t ch = s[0];

    if (ch == '\0') {
        if (tr->term && tr->child_n == 0) {
            *stat = TRIE_MATCH;
        } else if (tr->term && tr->child_n > 0) {
            *stat = TRIE_AMBIG;
        } else if (!tr->term && tr->child_n > 0) {
            *stat = TRIE_WAIT;
        } else if (!tr->term && tr->child_n == 0)
            *stat = TRIE_NONE;

        return tr->data;
    }

    if (!tr->children[ch]) {
        *stat = TRIE_NONE;
        return NULL;
    }

    return trie_lookup(tr->children[ch], &s[1], stat);
}

#undef _TRIE_CHILD_MAX
#endif

/*------------------------------------
args.h
------------------------------------*/

/*  Define an array of opt_data for the opts you want to parse.

    For each opt_data element, arg_parse sets 'found' to true and one of
    the union members to the corresponding opt arg if, respectively, the
    option was found and the option takes an argument.

    If you define an enum of option names...

    enum opt_names {
        VERBOSE,
        PORT,
        LOGFD,
    };

    opt_data *opts = {
        [VERBOSE] = { ... },
        [PORT] = { ... },
        [LOGFD] = {... }
    };

    ...then you can access options like so:

    if (opts[LOGFD].found)
        logfd = opts[LOGFD].val_int;  */

#ifndef LINC_TOOLS_ARGS_H
#define LINC_TOOLS_ARGS_H

#include <stddef.h>
#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <string.h>


#define ARGS_SILENT 1
#define ARGS_NO_NON_OPTS 2

typedef enum {
    ARGS_INT_ARG,
    ARGS_STR_ARG,
} args_opt_arg_type;

typedef enum {
    ARGS_NO_ARG = 0,
    ARGS_REQUIRED_ARG,
    ARGS_OPTIONAL_ARG,
} args_opt_arg_info;

typedef struct {
    char *long_name;
    char short_name;
    args_opt_arg_info has_arg;
    args_opt_arg_type arg_type;
    bool found;
    bool opt_arg_found;
    union {
        int val_int;
        char *val_str;
    };
} args_opt_data;

__attribute__ ((format(printf, 1, 2)))
static inline void _args_report_err(const char *fmt, ...) {
    if (opterr == 0)
        return;

    va_list va;

    va_start(va, fmt);
    vfprintf(stderr, fmt, va);
    va_end(va);
}

static inline int _args_get_int(char *s, int *out) {
    assert(s && out);

    if (*s == '\0')
        return -1;

    char *endptr;
    long num = strtol(s, &endptr, 10);

    if (num < INT_MIN || num > INT_MAX)
        return -1;

    if (*endptr != '\0')
        return -1;

    *out = num;
    return 0;
}

static inline void _args_parse_opt_arg_str(char *opt_arg, args_opt_data *data) {
    if (!opt_arg)
        return;

    data->val_str = opt_arg;
}

static inline int _args_parse_opt_arg_int(char *opt_arg, args_opt_data *data) {
    if (!opt_arg) {
        data->opt_arg_found = false;
        return 0;
    }

    int num;
    if (_args_get_int(opt_arg, &num) == -1)
        return -1;

    data->val_int = num;
    data->opt_arg_found = true;

    return 0;
}

static inline args_opt_data *_args_lookup_opt_by_short_opt(args_opt_data *opts,
        char short_opt)
{
    for (args_opt_data *opt = opts;; ++opt) {
        if (memcmp(opt, &(args_opt_data){0}, sizeof(args_opt_data)) == 0)
            break;

        if (short_opt == opt->short_name)
            return opt;
    }

    return NULL;
}

static inline int args_parse(int argc, char * const *argv, args_opt_data *opts,
        int flags)
{
    assert(argv && opts);

    optind = 0;

    int saved_opterr = opterr;

    if (flags & ARGS_SILENT)
        opterr = 0;

    size_t opt_n = 0;
    for (args_opt_data *opt = opts;; ++opt) {
        if (memcmp(opt, &(args_opt_data){0}, sizeof(args_opt_data)) == 0)
            break;
        ++opt_n;
    }

    int long_optind;

    struct option *long_opts = calloc(opt_n + 1, sizeof(*long_opts));
    if (!long_opts)
        _LINC_LIB_FATAL("calloc: out of memory");

    char *short_opts = calloc(sizeof(char), opt_n * 3 + 1);
    if (!short_opts)
        _LINC_LIB_FATAL("calloc: out of memory");

    for (size_t i = 0, y = 0; i < opt_n; ++i, ++y) {
        long_opts[i].name    = opts[i].long_name;
        long_opts[i].val     = opts[i].short_name;
        long_opts[i].has_arg = opts[i].has_arg;
        long_opts[i].flag    = NULL;

        short_opts[y] = opts[i].short_name;

        if (opts[i].has_arg == ARGS_REQUIRED_ARG)
            short_opts[++y] = ':';
        if (opts[i].has_arg == ARGS_OPTIONAL_ARG) {
            short_opts[++y] = ':';
            short_opts[++y] = ':';
        }
    }

    while (true) {
        char c = getopt_long(argc, argv, short_opts, long_opts, &long_optind);

        if (c == -1) /* done parsing */
            break;

        if (c == '?') /* unrecognized option or no arg when required */
            goto fail;

        args_opt_data *opt = _args_lookup_opt_by_short_opt(opts, c);
        if (!opt) {
            _args_report_err("%s: unexpected opt value: %c\n", argv[0], c);
            goto fail;
        }

        opt->found = true;

        if (opt->has_arg == ARGS_NO_ARG)
            continue;

        switch (opt->arg_type) {
        case ARGS_INT_ARG:
            if (_args_parse_opt_arg_int(optarg, opt) == -1) {
                _args_report_err("%s: bad arg to '--%s' -- '%s'\n",
                        argv[0], opt->long_name, optarg);
                goto fail;
            }
            break;
        case ARGS_STR_ARG:
            _args_parse_opt_arg_str(optarg, opt);
            break;
        }
    }

    if (flags & ARGS_NO_NON_OPTS) {
        if (optind < argc) {
            _args_report_err("%s: unrecognized argv elements: ", argv[0]);
            while (optind < argc)
                _args_report_err("%s ", argv[optind++]);
            _args_report_err("\n");

            goto fail;
        }
    }

    free(long_opts);
    free(short_opts);
    opterr = saved_opterr;

    return 0;

fail:
    free(long_opts);
    free(short_opts);
    opterr = saved_opterr;

    return -1;
}

#endif

/*------------------------------------
kval.h
------------------------------------*/

#ifndef LINC_TOOLS_KVAL
#define LINC_TOOLS_KVAL

#include <stddef.h>
#include <stdlib.h>
#include <string.h>


typedef struct {
    void *k;
    void *v;
} kvp;

typedef struct {
    kvp *pairs;
    size_t k_sz;
    size_t v_sz;
} kval;

typedef void *(* k_fn)(kval *, const void *);

#define kv_foreach(kv, x) \
    for (size_t i = 0; i < hda_size(kv->pairs); ++i) if ((x = kv->pairs + i))

#define kv_add(kv, k, v) \
    _Generic(k, \
        char *: kv_add_imp(kv, &(typeof(k)){k}, &(typeof(v)){v}, str_canon_k), \
        int: kv_add_imp(kv, &(typeof(k)){k}, &(typeof(v)){v}, num_canon_k) \
    )

#define kv_set(kv, k, v) \
    _Generic(k, \
        char *: kv_set_imp(kv, &(typeof(k)){k}, &(typeof(v)){v}, str_canon_k), \
        int: kv_set_imp(kv, &(typeof(k)){k}, &(typeof(v)){v}, num_canon_k) \
    )

#define kv_lookup(kv, k) \
    _Generic(k, \
        char *: kv_lookup_imp(kv, &(typeof(k)){k}, str_canon_k), \
        int: kv_lookup_imp(kv, &(typeof(k)){k}, num_canon_k) \
    )

#define kv_delete(kv, k) \
    _Generic(k, \
        char *: kv_delete_imp(kv, &(typeof(k)){k}, str_canon_k), \
        int: kv_delete_imp(kv, &(typeof(k)){k}, num_canon_k) \
    )

static inline void *lookup_idx(kval *kv, void *k, size_t *idx) {
    for (size_t i = 0; i < hda_size(kv->pairs); ++i) {

        if (memcmp(k, kv->pairs[i].k, kv->k_sz) == 0) {
            if (idx)
                *idx = i;
            return kv->pairs[i].v;
        }
    }

    return NULL;
}

static inline int kv_add_imp(kval *kv, void *k, void *v, k_fn fn) {
    char *canon_k = fn(kv, k);

    if (lookup_idx(kv, canon_k, NULL)) {
        free(canon_k);
        return -1;
    }

    kvp *el = hda_push(kv->pairs, { canon_k, nullptr });

    el->v = malloc(kv->v_sz);
    if (!el->v)
        _LINC_LIB_FATAL("malloc: out of memory");

    memcpy(el->v, v, kv->v_sz);

    return 0;
}

static inline int kv_set_imp(kval *kv, void *k, void *v, k_fn fn) {
    char *canon_k = fn(kv, k);
    size_t idx;

    if (lookup_idx(kv, canon_k, &idx)) {
        free(kv->pairs[idx].k);
        free(kv->pairs[idx].v);
        hda_delete(kv->pairs, idx, 1);
    }

    kvp *el = hda_push(kv->pairs, { canon_k, nullptr });

    el->v = malloc(kv->v_sz);
    if (!el->v)
        _LINC_LIB_FATAL("malloc: out of memory");

    memcpy(el->v, v, kv->v_sz);

    return 0;
}

static inline void *kv_lookup_imp(kval *kv, const void *k, k_fn fn) {
    assert(kv);
    assert(k);

    void *canon_k = fn(kv, k);

    void *v = lookup_idx(kv, canon_k, NULL);
    if (!v) {
        free(canon_k);
        return NULL;
    }

    free(canon_k);
    return v;
}

static inline int kv_delete_imp(kval *kv, void *k, k_fn fn) {
    void *canon_k = fn(kv, k);

    size_t idx;
    if (!lookup_idx(kv, canon_k, &idx)) {
        free(canon_k);
        return 0;
    }

    free(kv->pairs[idx].k);
    free(kv->pairs[idx].v);
    hda_delete(kv->pairs, idx, 1);

    free(canon_k);
    return 0;
}

static inline void *str_canon_k(kval *kv, const void *k) {
    assert(kv);
    assert(k);
    assert(strlen(k) < kv->k_sz);

    void *canon_k = calloc(kv->k_sz, 1);
    if (!canon_k)
        _LINC_LIB_FATAL("calloc: out of memory");

    strcpy(canon_k, k);
    return canon_k;
}

static inline void *num_canon_k(kval *kv, const void *k) {
    assert(kv && k);

    int *canon_k = calloc(kv->k_sz, 1);
    if (!canon_k)
        _LINC_LIB_FATAL("calloc: out of memory");

    *canon_k = *(int *)k;
    return canon_k;
}

static inline kval *kv_create(size_t k_sz, size_t v_sz) {
    kval *kv = malloc(sizeof(kval));
    if (!kv)
        _LINC_LIB_FATAL("malloc: out of memory");

    kv->pairs = nullptr;
    kv->k_sz = k_sz;
    kv->v_sz = v_sz;

    return kv;
}

static inline void kv_free(kval *kv) {
    for (size_t i = 0; i < hda_size(kv->pairs); ++i) {
        free(kv->pairs[i].k);
        free(kv->pairs[i].v);
    }

    hda_free(kv->pairs);
    free(kv);
}

#endif

/*------------------------------------
llog.h
------------------------------------*/

#ifndef LINC_TOOLS_LLOG_H
#define LINC_TOOLS_LLOG_H

#include <assert.h>
#include <execinfo.h>
#include <stdlib.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stddef.h>
#include <unistd.h>


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
         _LINC_LIB_FATAL("llog: vasprintf: allocation or IO error");
     va_end(va);

    _llog_convert_special_chars(&info.msg);
    log->sink(&info);
    free(info.msg);

    errno = saved_errno;
    return;
}

#endif

/*------------------------------------
splitty.h
------------------------------------*/

#ifndef LINC_TOOLS_SPLITTY_H
#define LINC_TOOLS_SPLITTY_H

#include <stddef.h>
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>


#define SP_MOVED 1
#define SP_RESIZED 2
#define _SP_CHILD_MAX 100

typedef struct sp_node sp_node;
typedef struct sp_rect sp_rect;
typedef int (sp_init_fn)(sp_node *node);
typedef void (sp_resize_fn)(sp_node *node, uint32_t flags);
typedef void (sp_free_fn)(sp_node *node);

typedef enum {
    SP_SPLIT_LEFT,
    SP_SPLIT_RIGHT,
    SP_SPLIT_GAP,
    SP_SPLIT_CHILD_MAX,
} sp_split_child;

typedef enum {
    SP_QUAD_TL,
    SP_QUAD_TR,
    SP_QUAD_BL,
    SP_QUAD_BR,

    SP_QUAD_GL,
    SP_QUAD_GR,
    SP_QUAD_GT,
    SP_QUAD_GB,
    SP_QUAD_GJ,
    SP_QUAD_CHILD_MAX,
} sp_quad_child;

typedef enum {
    SP_SPLIT,
    SP_QUAD,
    SP_LEAF,
} sp_node_type;

typedef enum {
    SP_HSPLIT,
    SP_VSPLIT,
} sp_axis;

typedef struct {
    float ratio;
    sp_axis axis;
    int split_gap;
    sp_node *left;
    sp_node *right;
    sp_node *gap;
} sp_split_config;

typedef struct {
    float ratio_y;
    float ratio_x;
    int quad_gap;
    sp_node *tl, *tr, *bl, *br;
    sp_node *gl, *gr, *gt, *gb, *gj;
} sp_quad_config;

struct sp_vec {
    int y;
    int x;
};

struct sp_rect {
    int y;
    int x;
    int rows;
    int cols;
};

struct sp_node {
    sp_rect rect;
    sp_node_type type;

    union {
        /* container */
        struct {
            size_t child_n;
            sp_node *children[_SP_CHILD_MAX];

            union {
                /* split */
                struct {
                    float ratio;
                    sp_axis axis;
                    int split_gap;
                };

                /* quad */
                struct {
                    float ratio_x;
                    float ratio_y;
                    int quad_gap;
                };
            };
        };

        /* leaf */
        struct {
            bool is_data_init;
            sp_init_fn *init_data;
            sp_resize_fn *resize;
            sp_free_fn *free_data;
            void *user_data;
        };
    };
};

static inline void _sp_calc_quad(sp_node *node, sp_rect rects[_SP_CHILD_MAX]) {
    int gap = node->quad_gap;

    if (gap > node->rect.rows)
        gap = node->rect.rows;

    if (gap > node->rect.cols)
        gap = node->rect.cols;

    int avail_rows = node->rect.rows - gap;
    int avail_cols = node->rect.cols - gap;

    int top_side_rows = avail_rows * node->ratio_y;
    int top_side_y = node->rect.y;

    int bottom_side_rows = avail_rows - top_side_rows;
    int bottom_side_y = top_side_y + top_side_rows + gap;

    int left_side_cols = avail_cols * node->ratio_x;
    int left_side_x = node->rect.x;

    int right_side_cols = avail_cols - left_side_cols;
    int right_side_x = left_side_x + left_side_cols + gap;

    rects[SP_QUAD_TL].rows = top_side_rows;
    rects[SP_QUAD_TL].cols = left_side_cols;
    rects[SP_QUAD_TL].y = top_side_y;
    rects[SP_QUAD_TL].x = left_side_x;

    rects[SP_QUAD_TR].rows = top_side_rows;
    rects[SP_QUAD_TR].cols = right_side_cols;
    rects[SP_QUAD_TR].y = top_side_y;
    rects[SP_QUAD_TR].x = right_side_x;

    rects[SP_QUAD_BL].rows = bottom_side_rows;
    rects[SP_QUAD_BL].cols = left_side_cols;
    rects[SP_QUAD_BL].y = bottom_side_y;
    rects[SP_QUAD_BL].x = left_side_x;

    rects[SP_QUAD_BR].rows = bottom_side_rows;
    rects[SP_QUAD_BR].cols = right_side_cols;
    rects[SP_QUAD_BR].y = bottom_side_y;
    rects[SP_QUAD_BR].x = right_side_x;

    int y_gap_y = top_side_y + top_side_rows;
    int x_gap_x = left_side_x + left_side_cols;

    rects[SP_QUAD_GL].rows = gap;
    rects[SP_QUAD_GL].cols = left_side_cols;
    rects[SP_QUAD_GL].y = y_gap_y;
    rects[SP_QUAD_GL].x = left_side_x;

    rects[SP_QUAD_GR].rows = gap;
    rects[SP_QUAD_GR].cols = right_side_cols;
    rects[SP_QUAD_GR].y = y_gap_y;
    rects[SP_QUAD_GR].x = right_side_x;

    rects[SP_QUAD_GT].rows = top_side_rows;
    rects[SP_QUAD_GT].cols = gap;
    rects[SP_QUAD_GT].y = top_side_y;
    rects[SP_QUAD_GT].x = x_gap_x;

    rects[SP_QUAD_GB].rows = bottom_side_rows;
    rects[SP_QUAD_GB].cols = gap;
    rects[SP_QUAD_GB].y = bottom_side_y;
    rects[SP_QUAD_GB].x = x_gap_x;

    rects[SP_QUAD_GJ].rows = gap;
    rects[SP_QUAD_GJ].cols = gap;
    rects[SP_QUAD_GJ].y = y_gap_y;
    rects[SP_QUAD_GJ].x = x_gap_x;
}

static inline void _sp_calc_split(sp_node *node, sp_rect rects[_SP_CHILD_MAX]) {
    int gap = node->split_gap;

    rects[SP_SPLIT_LEFT] = node->rect;
    rects[SP_SPLIT_RIGHT] = node->rect;
    rects[SP_SPLIT_GAP] = node->rect;

    if (node->axis == SP_HSPLIT) {
        if (node->split_gap > node->rect.rows)
            gap = node->rect.rows;

        int avail = node->rect.rows - gap;

        rects[SP_SPLIT_LEFT].rows = avail * node->ratio;

        rects[SP_SPLIT_GAP].rows = gap;
        rects[SP_SPLIT_GAP].y += rects[SP_SPLIT_LEFT].rows;

        rects[SP_SPLIT_RIGHT].rows = avail - rects[SP_SPLIT_LEFT].rows;
        rects[SP_SPLIT_RIGHT].y += rects[SP_SPLIT_LEFT].rows + gap;
    }

    if (node->axis == SP_VSPLIT) {
        if (node->split_gap > node->rect.cols)
            gap = node->rect.cols;

        int avail = node->rect.cols - node->split_gap;

        rects[SP_SPLIT_LEFT].cols = avail * node->ratio;

        rects[SP_SPLIT_GAP].cols = gap;
        rects[SP_SPLIT_GAP].x += rects[SP_SPLIT_LEFT].cols;

        rects[SP_SPLIT_RIGHT].cols = avail - rects[SP_SPLIT_LEFT].cols;
        rects[SP_SPLIT_RIGHT].x += rects[SP_SPLIT_LEFT].cols + node->split_gap;
    }
}

static inline sp_node *sp_new_quad(sp_quad_config cfg) {
    assert(cfg.ratio_x > 0 && cfg.ratio_x <= 1.0F);
    assert(cfg.ratio_y > 0 && cfg.ratio_y <= 1.0F);
    assert(cfg.quad_gap >= 0);
    assert(cfg.tl && cfg.tr && cfg.bl && cfg.br);
    assert(cfg.gl && cfg.gr && cfg.gt && cfg.gb && cfg.gj);

    sp_node *quad = malloc(sizeof(sp_node));
    if (!quad)
        _LINC_LIB_FATAL("malloc: out of memory");

    *quad = (sp_node){
        .type = SP_QUAD,
        .ratio_x = cfg.ratio_x,
        .ratio_y = cfg.ratio_y,
        .quad_gap = cfg.quad_gap,
        .child_n = SP_QUAD_CHILD_MAX,
    };

    quad->children[SP_QUAD_TL] = cfg.tl;
    quad->children[SP_QUAD_TR] = cfg.tr;
    quad->children[SP_QUAD_BL] = cfg.bl;
    quad->children[SP_QUAD_BR] = cfg.br;
    quad->children[SP_QUAD_GL] = cfg.gl;
    quad->children[SP_QUAD_GR] = cfg.gr;
    quad->children[SP_QUAD_GT] = cfg.gt;
    quad->children[SP_QUAD_GB] = cfg.gb;
    quad->children[SP_QUAD_GJ] = cfg.gj;

    return quad;
}

static inline sp_node *sp_new_split(sp_split_config cfg) {
    assert(cfg.ratio > 0 && cfg.ratio <= 1.0F);
    assert(cfg.left && cfg.right);
    assert(cfg.split_gap >= 0);

    sp_node *split = malloc(sizeof(sp_node));
    if (!split)
        _LINC_LIB_FATAL("malloc: out of memory");

    *split = (sp_node){
        .type = SP_SPLIT,
        .ratio = cfg.ratio,
        .axis = cfg.axis,
        .split_gap = cfg.split_gap,
        .child_n = SP_SPLIT_CHILD_MAX,
    };

    split->children[SP_SPLIT_LEFT] = cfg.left;
    split->children[SP_SPLIT_RIGHT] = cfg.right;
    split->children[SP_SPLIT_GAP] = cfg.gap;

    return split;
}

static inline sp_node *sp_new_leaf(sp_init_fn *fn1, sp_resize_fn *fn2,
        sp_free_fn *fn3)
{
    assert(fn1 && fn2 && fn3);

    sp_node *leaf = malloc(sizeof(sp_node));
    if (!leaf)
        _LINC_LIB_FATAL("malloc: out of memory");

    *leaf = (sp_node){
        .type = SP_LEAF,
        .is_data_init = false,
        .init_data = fn1,
        .resize = fn2,
        .free_data = fn3,
    };

    return leaf;
}

static inline void sp_refresh(sp_node *node, sp_rect rect) {
    assert(node);

    uint32_t change_flags = 0;

    if (rect.y != node->rect.y || rect.x != node->rect.x)
        change_flags |= SP_MOVED;

    if (rect.rows != node->rect.rows || rect.cols != node->rect.cols)
        change_flags |= SP_RESIZED;

    node->rect = rect;

    if (node->type == SP_LEAF) {
        if (!node->is_data_init) {
            if (node->init_data(node) == 0)
                node->is_data_init = true;

        } else if (change_flags != 0)
            node->resize(node, change_flags);

    } else {
        sp_rect rects[_SP_CHILD_MAX];

        switch (node->type) {
        case SP_SPLIT: _sp_calc_split(node, rects); break;
        case SP_QUAD:  _sp_calc_quad(node, rects);  break;
        default: _LINC_LIB_FATAL("splitty: unexpected node type");
        }

        for (size_t i = 0; i < node->child_n; ++i)
            sp_refresh(node->children[i], rects[i]);
    }
}

static inline void sp_free(sp_node *node) {
    if (node->type == SP_LEAF) {
        node->free_data(node);

    } else {
        for (size_t i = 0; i < node->child_n; ++i)
            sp_free(node->children[i]);
    }

    free(node);
}

#undef _SP_CHILD_MAX
#endif

/*------------------------------------
xfuncs.h
------------------------------------*/

#ifndef LINC_TOOLS_XFUNCS_H
#define LINC_TOOLS_XFUNCS_H

#include <fcntl.h>
#include <stdint.h>
#include <signal.h>
#include <termios.h>
#include <pty.h>
#include <poll.h>
#include <sys/timerfd.h>
#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <execinfo.h>
#include <dirent.h>
#include <errno.h>

#define XFILE     info->site->file
#define XFUNC     info->site->func
#define XLINE     info->site->line
#define XSYSNAME  info->sys_name
#define XERRNO    info->saved_errno
#define XPID      info->pid
#define XPPID     info->ppid
#define XPGID     info->pgid
#define XTID      info->tid

#define XFATAL_HANDLER(name) \
    __attribute__ ((__noreturn__)) \
    void name(const _xfuncs_info *info [[maybe_unused]], ...)

/* ONLY USE THE XFUNCS MACROS WITH xfatal_default() IF YOU
 * WILL NOT BE INCLUDED INTO ANOTHER PROJECT THAT MAY
 * ALSO USE THE XFUNCS MACROS WITH xfatal_default().
 *
 * LIBRARIES SHOULD USE x(syscall)_at() FUNCS EXPLICITLY */

#define XFATAL_DEFAULT(xf) \
    xfatal *xfatal_default(void) { \
        assert(xf); \
        return xf; \
    }

#define _XFUNCS_SYS_FAIL(xf, _site, _sys_name, ...) \
    do { \
        assert(xf); \
        if (xf->handler) { \
            _xfuncs_info info = { .site = _site }; \
            _xfuncs_collect_xinfo(&info, #_sys_name); \
            xf->handler(&info __VA_OPT__(,) __VA_ARGS__); \
            abort(); \
        } else \
            exit(EXIT_FAILURE); \
    } while (false)

typedef enum {
    XFUNCS_AVAIL_SID = 1,
    XFUNCS_AVAIL_BTRACE = 2,
} xfuncs_available_flags;

typedef struct {
    const char *file;
    const char *func;
    int line;
} _xfuncs_site;

typedef struct {
    /* Guaranteed */
    int saved_errno;
    const _xfuncs_site *site;
    const char *sys_name;
    pid_t pid;
    pid_t ppid;
    pid_t pgid;
    pid_t tid;

    /* Best-effort */
    uint64_t available;
    pid_t sid;
    int backtrace_count;
    char **backtrace;
} _xfuncs_info;

typedef void (xfatal_handler)(const _xfuncs_info *, ...);

typedef struct {
    xfatal_handler *handler;
} xfatal;

xfatal *xfatal_default(void);

static inline void _xfuncs_collect_xinfo(_xfuncs_info *info,
        const char *sys_name)
{
#define _XFUNCS_BT_BUF_SIZE 100
    info->saved_errno = errno; /* do this first! */

    /* Guaranteed */

    info->sys_name = sys_name;
    info->pid = getpid();
    info->ppid = getppid();
    info->pgid = getpgrp();
    info->tid = gettid();

    /* Best-effort */

    info->sid = getsid(0);
    if (info->sid != (pid_t)-1)
        info->available |= XFUNCS_AVAIL_SID;

    void *bt[_XFUNCS_BT_BUF_SIZE];
    int nptr = backtrace(bt, _XFUNCS_BT_BUF_SIZE);
    if (nptr < _XFUNCS_BT_BUF_SIZE && nptr > 2) {
        info->backtrace = backtrace_symbols(bt + 2, nptr - 2);
        if (info->backtrace) {
            info->backtrace_count = nptr - 2;
            info->available |= XFUNCS_AVAIL_BTRACE;
        }
    }
#undef _XFUNCS_BT_BUF_SIZE
}

#define _XFUNCS_SITE \
    (_xfuncs_site){ \
        .file = __FILE__, \
        .func = __func__, \
        .line = __LINE__, \
    }

#define xopen(...)            _xopen_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xdup2(...)            _xdup2_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xclose(...)           _xclose_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xmalloc(...)          _xmalloc_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xcalloc(...)          _xcalloc_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xrealloc(...)         _xrealloc_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xsigaction(...)       _xsigaction_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xsigemptyset(...)     _xsigemptyset_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xsigaddset(...)       _xsigaddset_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xsigdelset(...)       _xsigdelset_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xsigprocmask(...)     _xsigprocmask_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xsetpgid(...)         _xsetpgid_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xtcsetpgrp(...)       _xtcsetpgrp_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xgetcwd(...)          _xgetcwd_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xkill(...)            _xkill_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xatexit(...)          _xatexit_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xpipe(...)            _xpipe_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xpipe2(...)           _xpipe2_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xfork()               _xfork_at(xfatal_default(), &_XFUNCS_SITE)
#define xtcgetattr(...)       _xtcgetattr_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xtcsetattr(...)       _xtcsetattr_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xforkpty(...)         _xforkpty_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xpoll(...)            _xpoll_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xtimerfd_create(...)  _xtimerfd_create_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xtimerfd_settime(...) _xtimerfd_settime_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xstrdup(...)          _xstrdup_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xstrndup(...)         _xstrndup_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xsetvbuf(...)         _xsetvbuf_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xopendir(...)         _xopendir_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xclosedir(...)        _xclosedir_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xmkdir(...)           _xmkdir_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xrmdir(...)           _xrmdir_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xunlink(...)          _xunlink_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)
#define xchdir(...)           _xchdir_at(xfatal_default(), &_XFUNCS_SITE, __VA_ARGS__)

static inline int
_xopen_at(xfatal *xf, _xfuncs_site *site, const char *pathname, int flags, ...) {
    va_list va;
    mode_t mode = 0;

    if (flags & O_CREAT || (flags & O_TMPFILE) == O_TMPFILE) {
        va_start(va, flags);
        mode = va_arg(va, mode_t);
        va_end(va);
    }

    int rv = open(pathname, flags, mode);
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, open, pathname, flags);

    return rv;
}

static inline int
_xdup2_at(xfatal *xf, _xfuncs_site *site, int oldfd, int newfd) {
    int rv = dup2(oldfd, newfd);
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, dup2, oldfd, newfd);

    return rv;
}

static inline int
_xclose_at(xfatal *xf, _xfuncs_site *site, int fd) {
    int rv = close(fd);
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, close, fd);

    return rv;
}

static inline void *
_xmalloc_at(xfatal *xf, _xfuncs_site *site, int size) {
    void *rv = malloc(size);
    if (!rv)
        _XFUNCS_SYS_FAIL(xf, site, malloc, size);

    return rv;
}

static inline void *
_xcalloc_at(xfatal *xf, _xfuncs_site *site, size_t nmemb, size_t size) {
    void *rv = calloc(nmemb, size);
    if (!rv)
        _XFUNCS_SYS_FAIL(xf, site, calloc, nmemb, size);

    return rv;
}

static inline void *
_xrealloc_at(xfatal *xf, _xfuncs_site *site, void *ptr, int size) {
    void *rv = realloc(ptr, size);
    if (!rv)
        _XFUNCS_SYS_FAIL(xf, site, realloc, ptr, size);

    return rv;
}

static inline int
_xsigaction_at(xfatal *xf, _xfuncs_site *site, int signum,
        const struct sigaction *act, struct sigaction *oldact) {
    int rv = sigaction(signum, act, oldact);
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, sigaction, signum, act, oldact);

    return rv;
}

static inline int
_xsigemptyset_at(xfatal *xf, _xfuncs_site *site, sigset_t *set) {
    int rv = sigemptyset(set);
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, sigemptyset, set);

    return rv;
}

static inline int
_xsigaddset_at(xfatal *xf, _xfuncs_site *site, sigset_t *set, int signum) {
    int rv = sigaddset(set, signum);
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, sigaddset, set, signum);

    return rv;
}

static inline int
_xsigdelset_at(xfatal *xf, _xfuncs_site *site, sigset_t *set, int signum) {
    int rv = sigdelset(set, signum);
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, sigdelset, set, signum);

    return rv;
}

static inline int
_xsigprocmask_at(xfatal *xf, _xfuncs_site *site, int how, const sigset_t *set,
        sigset_t *oldset) {
    int rv = sigprocmask(how, set, oldset);
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, sigprocmask, how, set, oldset);

    return rv;
}

static inline int
_xsetpgid_at(xfatal *xf, _xfuncs_site *site, pid_t pid, pid_t pgid) {
    int rv = setpgid(pid, pgid);
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, setpgid, pid, pgid);

    return rv;
}

static inline int
_xtcsetpgrp_at(xfatal *xf, _xfuncs_site *site, int fd, pid_t pgrp) {
    int rv = tcsetpgrp(fd, pgrp);
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, tcsetpgrp, fd, pgrp);

    return rv;
}

static inline char *
_xgetcwd_at(xfatal *xf, _xfuncs_site *site, char *buf, size_t size) {
    char *rv = getcwd(buf, size);
    if (!rv)
        _XFUNCS_SYS_FAIL(xf, site, getcwd, buf, size);

    return rv;
}

static inline int
_xkill_at(xfatal *xf, _xfuncs_site *site, pid_t pid, int sig) {
    int rv = kill(pid, sig);
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, kill, pid, sig);

    return rv;
}

static inline int
_xatexit_at(xfatal *xf, _xfuncs_site *site, void (*function)(void)) {
    int rv = atexit(function);
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, atexit, function);

    return rv;
}

static inline int
_xpipe_at(xfatal *xf, _xfuncs_site *site, int pipefd[2]) {
    int rv = pipe(pipefd);
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, pipe, pipefd);

    return rv;
}

static inline int
_xpipe2_at(xfatal *xf, _xfuncs_site *site, int pipefd[2], int flags) {
    int rv = pipe2(pipefd, flags);
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, pipe2, pipefd, flags);

    return rv;
}

static inline int
_xfork_at(xfatal *xf, _xfuncs_site *site) {
    int rv = fork();
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, fork);

    return rv;
}

static inline int
_xtcgetattr_at(xfatal *xf, _xfuncs_site *site, int fd, struct termios *tio) {
    int rv = tcgetattr(fd, tio);
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, tcgetattr, fd, tio);

    return rv;
}

static inline int
_xtcsetattr_at(xfatal *xf, _xfuncs_site *site, int fd, int optional_actions,
        const struct termios *termios_p)
{
    int rv = tcsetattr(fd, optional_actions, termios_p);
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, tcsetattr, fd, optional_actions, termios_p);

    return rv;
}


static inline int
_xforkpty_at(xfatal *xf, _xfuncs_site *site, int *amaster, char *name,
        const struct termios *tio, const struct winsize *winp) {
    int rv = forkpty(amaster, name, tio, winp);
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, forkpty, amaster, name, tio, winp);

    return rv;
}

static inline int
_xpoll_at(xfatal *xf, _xfuncs_site *site, struct pollfd *fds, nfds_t nfds,
        int timeout) {
    int rv = poll(fds, nfds, timeout);
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, poll, fds, nfds, timeout);

    return rv;
}

static inline int
_xtimerfd_create_at(xfatal *xf, _xfuncs_site *site, int clockid, int flags) {
    int rv = timerfd_create(clockid, flags);
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, timerfd_create, clockid, flags);

    return rv;
}

static inline int
_xtimerfd_settime_at(xfatal *xf, _xfuncs_site *site, int fd, int flags,
        const struct itimerspec *new_value, struct itimerspec *old_value) {
    int rv = timerfd_settime(fd, flags, new_value, old_value);
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, timerfd_settime, fd, flags, new_value, old_value);

    return rv;
}

static inline char *
_xstrdup_at(xfatal *xf, _xfuncs_site *site, const char *s) {
    char *rv = strdup(s);
    if (!rv)
        _XFUNCS_SYS_FAIL(xf, site, strdup, s);

    return rv;
}

static inline char *
_xstrndup_at(xfatal *xf, _xfuncs_site *site, const char *s, size_t n) {
    char *rv = strndup(s, n);
    if (!rv)
        _XFUNCS_SYS_FAIL(xf, site, strndup, s, n);

    return rv;
}

static inline int
_xsetvbuf_at(xfatal *xf, _xfuncs_site *site, FILE *stream, char *buf,
        int mode, size_t size) {
    int rv = setvbuf(stream, buf, mode, size);
    if (rv != 0)
        _XFUNCS_SYS_FAIL(xf, site, setvbuf, stream, buf, mode, size);

    return rv;
}

static inline DIR *
_xopendir_at(xfatal *xf, _xfuncs_site *site, const char *name) {
    DIR *rv = opendir(name);
    if (!rv)
        _XFUNCS_SYS_FAIL(xf, site, opendir, name);

    return rv;
}

static inline int
_xclosedir_at(xfatal *xf, _xfuncs_site *site, DIR *dirp) {
    int rv = closedir(dirp);
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, closedir, dirp);

    return rv;
}

static inline int
_xmkdir_at(xfatal *xf, _xfuncs_site *site, const char *pathname, mode_t mode) {
    int rv = mkdir(pathname, mode);
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, mkdir, pathname, mode);

    return rv;
}

static inline int
_xrmdir_at(xfatal *xf, _xfuncs_site *site, const char *pathname) {
    int rv = rmdir(pathname);
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, rmdir, pathname);

    return rv;
}

static inline int
_xunlink_at(xfatal *xf, _xfuncs_site *site, const char *pathname) {
    int rv = unlink(pathname);
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, unlink, pathname);

    return rv;
}

static inline int
_xchdir_at(xfatal *xf, _xfuncs_site *site, const char *pathname) {
    int rv = chdir(pathname);
    if (rv == -1)
        _XFUNCS_SYS_FAIL(xf, site, chdir, pathname);

    return rv;
}

#endif


#endif