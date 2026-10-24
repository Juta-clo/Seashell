#include <stddef.h>
#include <linc/xsys.h>

#include "arena.h"
#include "utils.h"

#define ARENA_BLOCK_SIZE 8192

DA_TYPE(blocks, void *);

struct Arena {
    blocks blocks;
    size_t block_n;
};

Arena *arena_create(void) {
    Arena *arena = xcalloc(1, sizeof(Arena));
    return arena;
}

void arena_destroy(Arena *arena) {
    ASSERT(arena);

    for (size_t i = 0; i < arena->block_n; ++i)
        free(arena->blocks.data[i]);

    da_free(&arena->blocks);
    free(arena);
}

void *arena_alloc(size_t size, void *ctx) {
    ASSERT(ctx);
    Arena *arena = ctx;

    void *p = xmalloc(size);
    *da_push(&arena->blocks) = p;

    arena->block_n++;

    return p;
}

void arena_free(void *ptr, size_t size, void *ctx) {
    (void)ptr;
    (void)size;
    (void)ctx;

    return;
}
