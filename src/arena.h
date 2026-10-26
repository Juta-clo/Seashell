#ifndef ARENA_H
#define ARENA_H

#include <stddef.h>

typedef struct Arena Arena;

Arena *arena_create(void);
void arena_destroy(Arena *arena);

void *arena_alloc(size_t size, void *ctx);
void arena_free(void *ptr, size_t size, void *ctx);

#endif
