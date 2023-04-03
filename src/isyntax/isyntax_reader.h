#pragma once

#include "isyntax.h"

typedef struct isyntax_tile_list_t {
    isyntax_tile_t* head;
    isyntax_tile_t* tail;
    int count;
    const char* dbg_name;
} isyntax_tile_list_t;

typedef struct isyntax_cache_t {
    isyntax_tile_list_t cache_list;
    // TODO(avirodov): GMutex mutex;
    // TODO(avirodov): int refcount;
    int target_cache_size;
    block_allocator_t ll_coeff_block_allocator;
    block_allocator_t h_coeff_block_allocator;
    int allocator_block_width;
    int allocator_block_height;
} isyntax_cache_t;

// TODO(avirodov): currently it is up to the caller to mutex-lock the cache while calling isyntax_read_tile_bgra().
// TODO(avirodov): scale probably should be called 'level' in API, and need to resolve clash with current 'level' variables in implementation. Or leave it scale?
isyntax_cache_t* isyntax_make_cache(const char* dbg_name, int cache_size, int block_width, int block_height);
void isyntax_destroy_and_free_cache(isyntax_cache_t* cache);
uint32_t* isyntax_read_tile_bgra(isyntax_cache_t* cache, isyntax_t* isyntax, int scale, int tile_x, int tile_y);
