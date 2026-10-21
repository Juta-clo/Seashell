#include <assert.h>
#include <string.h>

#include "map.h"

typedef void *(* key_func)(map_t *, const void *);

int mp_init(map_t *map, size_t key_size) {
    *map = (map_t){0};

    da_init(&map->pairs);

    map->key_size = key_size;

    return 0;
}

void mp_free(map_t *map) {
    for (size_t i = 0; i < map->pairs.size; ++i) {
        free(map->pairs.data[i].key);
        free(map->pairs.data[i].value);
    }

    da_free(&map->pairs);

    *map = (map_t){0};
}

static void *lookup_index(map_t *map, void *key, size_t *index) {
    for (size_t i = 0; i < map->pairs.size; ++i) {

        if (memcmp(key, map->pairs.data[i].key, map->key_size) == 0) {
            if (index)
                *index = i;
            return map->pairs.data[i].value;
        }
    }

    return NULL;
}

static int mp_add(map_t *map, void *key, void *value, key_func func) {
    char *canon_key = func(map, key);
    if (!canon_key)
        goto fail;

    mpair *pair;
    size_t index;

    if (!lookup_index(map, canon_key, &index)) {
        pair = da_push(&map->pairs);
        pair->key = canon_key;
        pair->value = value;
    } else {
        free(map->pairs.data[index].value);
        map->pairs.data[index].value = value;
        free(canon_key);
    }

    return 0;

fail:
    free(canon_key);
    return -1;
}

static int mp_lookup(map_t *map, const void *key, void **value, key_func func) {
    assert(map && key && value);

    void *canon_key = func(map, key);
    if (!canon_key)
        return -1;

    void *map_value = lookup_index(map, canon_key, NULL);
    if (!map_value) {
        free(canon_key);
        *value = NULL;
        return 0;
    }

    *value = map_value;
    free(canon_key);

    return 0;
}

static int mp_delete(map_t *map, void *key, key_func func) {
    void *canon_key = func(map, key);
    if (!canon_key)
        goto fail;

    size_t index;
    if (!lookup_index(map, canon_key, &index))
        goto fail;

    free(map->pairs.data[index].value);
    free(map->pairs.data[index].key);

    da_delete(&map->pairs, index);

    free(canon_key);
    return 0;

fail:
    free(canon_key);
    return -1;
}

static void *str_canon_key(map_t *map, const void *key) {
    assert(map && key && strlen(key) < map->key_size);

    void *canon_key = calloc(map->key_size, 1);
    if (!canon_key)
        return NULL;

    strcpy(canon_key, key);

    return canon_key;
}

int mp_str_add(map_t *map, char *key, void *value) {
    if (mp_add(map, key, value, str_canon_key) == -1)
        return -1;

    return 0;
}

int mp_str_lookup(map_t *map, const char *key, void **value) {
    if (mp_lookup(map, key, value, str_canon_key) == -1)
        return -1;

    return 0;
}

int mp_str_delete(map_t *map, char *key) {
    if (mp_delete(map, key, str_canon_key) == -1)
        return -1;

    return 0;
}

static void *num_canon_key(map_t *map, const void *key) {
    assert(map && key);

    int *canon_key = calloc(map->key_size, 1);
    if (!canon_key)
        return NULL;

    *canon_key = *(int *)key;

    return canon_key;
}

int mp_num_add(map_t *map, int key, void *value) {
    if (mp_add(map, &key, value, num_canon_key) == -1)
        return -1;

    return 0;
}

int mp_num_lookup(map_t *map, int key, void **value) {
    if (mp_lookup(map, &key, value, num_canon_key) == -1)
        return -1;

    return 0;
}

int mp_num_delete(map_t *map, int key) {
    if (mp_delete(map, &key, num_canon_key) == -1)
        return -1;

    return 0;
}
