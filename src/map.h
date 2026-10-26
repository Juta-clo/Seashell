#ifndef MAP_H
#define MAP_H

#include <linc/array.h>

struct mpair {
    void *key;
    void *value;
};

typedef struct mpair mpair;
LINC_ARRAY_TYPE(da_mpair, mpair);

struct map_t {
    da_mpair pairs;
    size_t key_size;
};

typedef struct map_t map_t;

int mp_init(map_t *map, size_t key_size);
void mp_free(map_t *map);

int mp_str_add(map_t *map, char *key, void *value);
int mp_str_lookup(map_t *map, const char *key, void **value);
int mp_str_delete(map_t *map, char *key);

int mp_num_add(map_t *map, int key, void *value);
int mp_num_lookup(map_t *map, int key, void **value);
int mp_num_delete(map_t *map, int key);

#endif
