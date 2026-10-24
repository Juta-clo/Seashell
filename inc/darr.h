#ifndef DYN_ARR_H
#define DYN_ARR_H

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
        exit(EXIT_FAILURE);

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
