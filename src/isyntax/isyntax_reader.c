/*
  BSD 2-Clause License

  Copyright (c) 2019-2023, Pieter Valkema, Alexandr Virodov

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions are met:

  1. Redistributions of source code must retain the above copyright notice, this
     list of conditions and the following disclaimer.

  2. Redistributions in binary form must reproduce the above copyright notice,
     this list of conditions and the following disclaimer in the documentation
     and/or other materials provided with the distribution.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
  DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
  FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
  DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
  SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
  OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "common.h"
#include "isyntax_reader.h"

#define LOG(msg, ...) console_print(msg, ##__VA_ARGS__)
#define LOG_VAR(fmt, var) console_print("%s: %s=" fmt "\n", __FUNCTION__, #var, var)

isyntax_cache_t* isyntax_cache_create(const char* debug_name_or_null, int32_t cache_size)
{
    isyntax_cache_t* cache_ptr = malloc(sizeof(isyntax_cache_t));
    memset(cache_ptr, 0, sizeof(*cache_ptr));
    tile_list_init(&cache_ptr->cache_list, debug_name_or_null);
    cache_ptr->target_cache_size = cache_size;
    cache_ptr->mutex = benaphore_create();

    // Note: rest of initialization is deferred to the first injection, as that is where we will know the block size.
    return cache_ptr;
}

void isyntax_cache_inject(isyntax_cache_t* isyntax_cache, isyntax_t* isyntax) {
    ASSERT(isyntax->ll_coeff_block_allocator == NULL);
    ASSERT(isyntax->h_coeff_block_allocator == NULL);

    if (!isyntax_cache->h_coeff_block_allocator.is_valid || !isyntax_cache->ll_coeff_block_allocator.is_valid) {
        // Shouldn't ever partially initialize.
        ASSERT(!isyntax_cache->h_coeff_block_allocator.is_valid);
        ASSERT(!isyntax_cache->ll_coeff_block_allocator.is_valid);

        isyntax_cache->allocator_block_width = isyntax->block_width;
        isyntax_cache->allocator_block_height = isyntax->block_height;
        size_t ll_coeff_block_size = isyntax->block_width * isyntax->block_height * sizeof(icoeff_t);
        size_t block_allocator_maximum_capacity_in_blocks = GIGABYTES(32) / ll_coeff_block_size;
        size_t ll_coeff_block_allocator_capacity_in_blocks = block_allocator_maximum_capacity_in_blocks / 4;
        size_t h_coeff_block_size = ll_coeff_block_size * 3;
        size_t h_coeff_block_allocator_capacity_in_blocks = ll_coeff_block_allocator_capacity_in_blocks * 3;
        isyntax_cache->ll_coeff_block_allocator = block_allocator_create(ll_coeff_block_size, ll_coeff_block_allocator_capacity_in_blocks, MEGABYTES(256));
        isyntax_cache->h_coeff_block_allocator = block_allocator_create(h_coeff_block_size, h_coeff_block_allocator_capacity_in_blocks, MEGABYTES(256));
    }

    // having multiple iSyntax with different block sizes opened with a shared cache is not supported.
    ASSERT(isyntax_cache->allocator_block_width == isyntax->block_width);
    ASSERT(isyntax_cache->allocator_block_height == isyntax->block_height);

    isyntax->ll_coeff_block_allocator = &isyntax_cache->ll_coeff_block_allocator;
    isyntax->h_coeff_block_allocator = &isyntax_cache->h_coeff_block_allocator;
    isyntax->is_block_allocator_owned = false;
}

void isyntax_cache_destroy(isyntax_cache_t* isyntax_cache) {
    if (isyntax_cache->ll_coeff_block_allocator.is_valid) {
        block_allocator_destroy(&isyntax_cache->ll_coeff_block_allocator);
    }
    if (isyntax_cache->h_coeff_block_allocator.is_valid) {
        block_allocator_destroy(&isyntax_cache->h_coeff_block_allocator);
    }
    benaphore_destroy(&isyntax_cache->mutex);
    free(isyntax_cache);
}

void isyntax_cache_trim(isyntax_cache_t* isyntax_cache, i32 target_size) {
    // TODO(avirodov): later will need to skip tiles that are reserved by other threads.
    while (isyntax_cache->cache_list.count > target_size) {
        isyntax_tile_t* tile = isyntax_cache->cache_list.tail;
        tile_list_remove(&isyntax_cache->cache_list, tile);
        for (int i = 0; i < 3; ++i) {
            if (tile->has_ll) {
                block_free(&isyntax_cache->ll_coeff_block_allocator, tile->color_channels[i].coeff_ll);
                tile->color_channels[i].coeff_ll = NULL;
            }
            if (tile->has_h) {
                block_free(&isyntax_cache->h_coeff_block_allocator, tile->color_channels[i].coeff_h);
                tile->color_channels[i].coeff_h = NULL;
            }
        }
        tile->has_ll = false;
        tile->has_h = false;
    }
}

void tile_list_init(isyntax_tile_list_t* list, const char* dbg_name) {
    list->head = NULL;
    list->tail = NULL;
    list->count = 0;
    list->dbg_name = dbg_name;
}

void tile_list_remove(isyntax_tile_list_t* list, isyntax_tile_t* tile) {
    if (!tile->cache_next && !tile->cache_prev && !(list->head == tile) && !(list->tail == tile)) {
        // Not part of any list.
        return;
    }
    if (list->head == tile) {
        list->head = tile->cache_next;
    }
    if (list->tail == tile) {
        list->tail = tile->cache_prev;
    }
    if (tile->cache_prev) {
        tile->cache_prev->cache_next = tile->cache_next;
    }
    if (tile->cache_next) {
        tile->cache_next->cache_prev = tile->cache_prev;
    }
    // Here we assume that the tile is part of this list, but we don't check (O(n)).
    tile->cache_next = NULL;
    tile->cache_prev = NULL;
    list->count--;
}

static void tile_list_insert_first(isyntax_tile_list_t* list, isyntax_tile_t* tile) {
    // printf("### tile_list_insert_first %s scale=%d x=%d y=%d\n", list->dbg_name, tile->tile_scale, tile->tile_x, tile->tile_y);
    ASSERT(tile->cache_next == NULL && tile->cache_prev == NULL);
    if (list->head == NULL) {
        list->head = tile;
        list->tail = tile;
    } else {
        list->head->cache_prev = tile;
        tile->cache_next = list->head;
        list->head = tile;
    }
    list->count++;
}

static void tile_list_insert_list_first(isyntax_tile_list_t* target_list, isyntax_tile_list_t* source_list) {
    if (source_list->head == NULL && source_list->tail == NULL) {
        return;
    }

    source_list->tail->cache_next = target_list->head;
    if (target_list->head) {
        target_list->head->cache_prev = source_list->tail;
    }

    target_list->head = source_list->head;
    if (target_list->tail == NULL) {
        target_list->tail = source_list->tail;
    }
    target_list->count += source_list->count;
    source_list->head = NULL;
    source_list->tail = NULL;
    source_list->count = 0;
}

#define ITERATE_TILE_LIST(_iter, _list) \
    isyntax_tile_t* _iter = _list.head; _iter; _iter = _iter->cache_next


static void isyntax_openslide_load_tile_coefficients_ll_or_h(isyntax_cache_t* cache,
                                                             isyntax_t* isyntax, isyntax_tile_t* tile,
                                                             int codeblock_index, bool is_ll) {
    isyntax_image_t* wsi = &isyntax->images[isyntax->wsi_image_index];
    isyntax_data_chunk_t* chunk = &wsi->data_chunks[tile->data_chunk_index];

    for (int color = 0; color < 3; ++color) {
        isyntax_codeblock_t* codeblock = &wsi->codeblocks[codeblock_index + color * chunk->codeblock_count_per_color];
        ASSERT(codeblock->coefficient == (is_ll ? 0 : 1)); // LL coefficient codeblock for this tile.
        // TODO(avirodov): int vs i32 vs u32 consistently.
        ASSERT(codeblock->color_component == (u32)color);
        ASSERT(codeblock->scale == (u32)tile->tile_scale);
        if (is_ll) {
            tile->color_channels[color].coeff_ll = (icoeff_t *) block_alloc(&cache->ll_coeff_block_allocator);
        } else {
            tile->color_channels[color].coeff_h = (icoeff_t *) block_alloc(&cache->h_coeff_block_allocator);
        }
        // TODO(avirodov): fancy allocators, for multiple sequential blocks (aka chunk). Or let OS do the caching.
        // Adding 7 safety bytes so bitstream_lsb_read() won't access out of bounds in isyntax_hulsken_decompress().
        u8* codeblock_data = malloc(codeblock->block_size + 7);
        size_t bytes_read = file_handle_read_at_offset(codeblock_data, isyntax->file_handle,
                                                       codeblock->block_data_offset, codeblock->block_size);
        if (!(bytes_read > 0)) {
            console_print_error("Error: could not read iSyntax data at offset %lld (read size %lld)\n",
                                codeblock->block_data_offset, codeblock->block_size);
        }

        isyntax_hulsken_decompress(codeblock_data, codeblock->block_size,
                                   isyntax->block_width, isyntax->block_height,
                                   codeblock->coefficient, 1,
                                   is_ll ? tile->color_channels[color].coeff_ll : tile->color_channels[color].coeff_h);
        free(codeblock_data);
    }

    if (is_ll) {
        tile->has_ll = true;
    } else {
        tile->has_h = true;
    }
}

static void isyntax_openslide_load_tile_coefficients(isyntax_cache_t* cache, isyntax_t* isyntax, isyntax_tile_t* tile) {
    isyntax_image_t* wsi = &isyntax->images[isyntax->wsi_image_index];

    if (!tile->exists) {
        return;
    }

    // Load LL codeblocks here only for top-level tiles. For other levels, the LL coefficients are computed from parent
    // tiles later on.
    if (!tile->has_ll && tile->tile_scale == wsi->max_scale) {
        isyntax_openslide_load_tile_coefficients_ll_or_h(
                cache, isyntax, tile, /*codeblock_index=*/tile->codeblock_index, /*is_ll=*/true);
    }

    if (!tile->has_h) {
        ASSERT(tile->exists);
        isyntax_data_chunk_t* chunk = wsi->data_chunks + tile->data_chunk_index;

        i32 scale_in_chunk = chunk->scale - tile->tile_scale;
        ASSERT(scale_in_chunk >= 0 && scale_in_chunk < 3);
        i32 codeblock_index_in_chunk = 0;
        if (scale_in_chunk == 0) {
            codeblock_index_in_chunk = 0;
        } else if (scale_in_chunk == 1) {
            codeblock_index_in_chunk = 1 + (tile->tile_y % 2) * 2 + (tile->tile_x % 2);
        } else if (scale_in_chunk == 2) {
            codeblock_index_in_chunk = 5 + (tile->tile_y % 4) * 4 + (tile->tile_x % 4);
        } else {
            panic();
        }

        isyntax_openslide_load_tile_coefficients_ll_or_h(
                cache, isyntax, tile,
                /*codeblock_index=*/tile->codeblock_chunk_index + codeblock_index_in_chunk, /*is_ll=*/false);
    }
}

typedef union isyntax_tile_children_t {
    struct {
        isyntax_tile_t *child_top_left;
        isyntax_tile_t *child_top_right;
        isyntax_tile_t *child_bottom_left;
        isyntax_tile_t *child_bottom_right;
    };
    isyntax_tile_t* as_array[4];
} isyntax_tile_children_t;

static isyntax_tile_children_t isyntax_openslide_compute_children(isyntax_t* isyntax, isyntax_tile_t* tile) {
    isyntax_tile_children_t result;
    isyntax_image_t* wsi = &isyntax->images[isyntax->wsi_image_index];
    ASSERT(tile->tile_scale > 0);
    isyntax_level_t *next_level = &wsi->levels[tile->tile_scale - 1];
    result.child_top_left = next_level->tiles + (tile->tile_y * 2) * next_level->width_in_tiles + (tile->tile_x * 2);
    result.child_top_right = result.child_top_left + 1;
    result.child_bottom_left = result.child_top_left + next_level->width_in_tiles;
    result.child_bottom_right = result.child_bottom_left + 1;
    return result;
}


static void isyntax_openslide_idwt(isyntax_cache_t* cache, isyntax_t* isyntax, isyntax_tile_t* tile,
                                   uint32_t* pixels_buffer, enum isyntax_pixel_format_t pixel_format) {
    if (tile->tile_scale == 0) {
        ASSERT(pixels_buffer != NULL); // Shouldn't be asking for idwt at level 0 if we're not going to use the result for pixels.
        isyntax_load_tile(isyntax, &isyntax->images[isyntax->wsi_image_index],
                                 tile->tile_scale, tile->tile_x, tile->tile_y,
                                 pixels_buffer, pixel_format);
        return;
    }

    if (pixels_buffer != NULL) {
        // TODO(avirodov): if we want rgb from tile where idwt was done already, this could be cheaper if we store
        //  the lls in the tile. Currently need to recompute idwt.
        isyntax_load_tile(isyntax, &isyntax->images[isyntax->wsi_image_index],
                          tile->tile_scale, tile->tile_x, tile->tile_y,
                          pixels_buffer, pixel_format);
        return;
    }

    // If all children have ll coefficients and we don't need the rgb pixels, no need to do the idwt.
    ASSERT(pixels_buffer == NULL && tile->tile_scale > 0);
    isyntax_tile_children_t children = isyntax_openslide_compute_children(isyntax, tile);
    if (children.child_top_left->has_ll && children.child_top_right->has_ll &&
        children.child_bottom_left->has_ll && children.child_bottom_right->has_ll) {
        return;
    }

    isyntax_load_tile(isyntax, &isyntax->images[isyntax->wsi_image_index],
                      tile->tile_scale, tile->tile_x, tile->tile_y,
                      /*pixels_buffer=*/NULL, /*pixel_format=*/0);
}

static void isyntax_make_tile_lists_add_parent_to_list(isyntax_t* isyntax, isyntax_tile_t* tile,
                                                       isyntax_tile_list_t* idwt_list, isyntax_tile_list_t* cache_list) {
    isyntax_image_t* wsi = &isyntax->images[isyntax->wsi_image_index];
    int parent_tile_scale = tile->tile_scale + 1;
    if (parent_tile_scale > wsi->max_scale) {
        return;
    }

    int parent_tile_x = tile->tile_x / 2;
    int parent_tile_y = tile->tile_y / 2;
    isyntax_level_t* parent_level = &wsi->levels[parent_tile_scale];
    isyntax_tile_t* parent_tile = &parent_level->tiles[parent_level->width_in_tiles * parent_tile_y + parent_tile_x];
    if (parent_tile->exists && !parent_tile->cache_marked) {
        tile_list_remove(cache_list, parent_tile);
        parent_tile->cache_marked = true;
        tile_list_insert_first(idwt_list, parent_tile);
    }
}

static void isyntax_make_tile_lists_add_children_to_list(isyntax_t* isyntax, isyntax_tile_t* tile,
                                                         isyntax_tile_list_t* children_list, isyntax_tile_list_t* cache_list) {
    if (tile->tile_scale > 0) {
        isyntax_tile_children_t children = isyntax_openslide_compute_children(isyntax, tile);
        for (int i = 0; i < 4; ++i) {
            if (!children.as_array[i]->cache_marked) {
                tile_list_remove(cache_list, children.as_array[i]);
                tile_list_insert_first(children_list, children.as_array[i]);
            }
        }
    }
}

static void isyntax_make_tile_lists_by_scale(isyntax_t* isyntax, int start_scale,
                                             isyntax_tile_list_t* idwt_list,
                                             isyntax_tile_list_t* coeff_list,
                                             isyntax_tile_list_t* children_list,
                                             isyntax_tile_list_t* cache_list) {
    isyntax_image_t* wsi = &isyntax->images[isyntax->wsi_image_index];
    for (int scale = start_scale; scale <= wsi->max_scale; ++scale) {
        // Mark all neighbors of idwt tiles at this level as requiring coefficients.
        isyntax_level_t* level = &wsi->levels[scale];
        for (ITERATE_TILE_LIST(tile, (*idwt_list))) {
            if (tile->tile_scale == scale) {
                for (int y_offset = -1; y_offset <= 1; ++y_offset) {
                    for (int x_offset = -1; x_offset <= 1; ++ x_offset) {
                        int neighbor_tile_x =  tile->tile_x + x_offset;
                        int neighbor_tile_y = tile->tile_y + y_offset;
                        if (neighbor_tile_x < 0 || neighbor_tile_x >= level->width_in_tiles ||
                            neighbor_tile_y < 0 || neighbor_tile_y >= level->height_in_tiles) {
                            continue;
                        }

                        isyntax_tile_t* neighbor_tile = &level->tiles[level->width_in_tiles * neighbor_tile_y + neighbor_tile_x];
                        if (neighbor_tile->cache_marked || !neighbor_tile->exists) {
                            continue;
                        }

                        tile_list_remove(cache_list, neighbor_tile);
                        neighbor_tile->cache_marked = true;
                        tile_list_insert_first(coeff_list, neighbor_tile);
                    }
                }
            }
        }

        // Mark all parents of tiles at this level as requiring idwt. This way all tiles at this level will get their
        // ll coefficients.
        for (ITERATE_TILE_LIST(tile, (*idwt_list))) {
            if (tile->tile_scale == scale) {
                isyntax_make_tile_lists_add_parent_to_list(isyntax, tile, idwt_list, cache_list);
            }
        }
        for (ITERATE_TILE_LIST(tile, (*coeff_list))) {
            if (tile->tile_scale == scale) {
                isyntax_make_tile_lists_add_parent_to_list(isyntax, tile, idwt_list, cache_list);
            }
        }
    }

    // Add all children of idwt that were not yet handled. The children will have their ll coefficients written,
    // and so should be cache bumped.
    // TODO(avirodov): if we store the idwt result (ll of next level) in the tile instead of the children, this
    //  would be unnecessary. But I'm not sure this is bad either.
    for (ITERATE_TILE_LIST(tile, (*idwt_list))) {
        isyntax_make_tile_lists_add_children_to_list(isyntax, tile, children_list, cache_list);
    }
}

void isyntax_tile_read(isyntax_t* isyntax, isyntax_cache_t* cache, int scale, int tile_x, int tile_y,
                       uint32_t* pixels_buffer, enum isyntax_pixel_format_t pixel_format) {
    // TODO(avirodov): more granular locking (some notes below). This will require handling overlapping work, that is
    //  thread A needing tile 123 and started to load it, and thread B needing same tile 123 and needs to wait for A.
    // TODO(pvalkema): Can we safely lock the mutex later, after checking if the tile exists?
    benaphore_lock(&cache->mutex);

    isyntax_image_t* wsi = &isyntax->images[isyntax->wsi_image_index];
    isyntax_level_t* level = &wsi->levels[scale];
    isyntax_tile_t *tile = &level->tiles[level->width_in_tiles * tile_y + tile_x];
    // printf("=== isyntax_openslide_load_tile scale=%d tile_x=%d tile_y=%d\n", scale, tile_x, tile_y);
    if (!tile->exists) {
        memset(pixels_buffer, 0xff, isyntax->tile_width * isyntax->tile_height * 4);
        benaphore_unlock(&cache->mutex);
        return;
    }

    // Need 3 lists:
    // 1. idwt list - those tiles will have to perform an idwt for their children to get ll coeffs. Primary cache bump.
    // 2. coeff list - those tiles are neighbors and will need to have coefficients loaded. Secondary cache bump.
    // 3. children list - those tiles will have their ll coeffs loaded as a side effect. Tertiary cache bump.
    // Those lists must be disjoint, and sorted such that parents are closer to head than children.
    isyntax_tile_list_t idwt_list = {NULL, NULL, 0, "idwt_list"};
    isyntax_tile_list_t coeff_list = {NULL, NULL, 0, "coeff_list"};
    isyntax_tile_list_t children_list = {NULL, NULL, 0, "children_list"};

    // Lock.
    // Make a list of all dependent tiles (including the required one).
    // Mark all dependent tiles as "reserved" so that they are not evicted by other threads as we load them.
    // Unlock.
    {
        tile_list_remove(&cache->cache_list, tile);
        tile->cache_marked = true;
        tile_list_insert_first(&idwt_list, tile);
    }
    isyntax_make_tile_lists_by_scale(isyntax, scale, &idwt_list, &coeff_list, &children_list, &cache->cache_list);

    // Unmark visit status and reserve all nodes. todo(avirodov): reserve later when doing threading.
    for (ITERATE_TILE_LIST(tile, idwt_list))     { tile->cache_marked = false; /*printf("@@@ idwt_list tile scale=%d x=%d y=%d\n", tile->tile_scale, tile->tile_x, tile->tile_y);*/ }
    for (ITERATE_TILE_LIST(tile, coeff_list))    { tile->cache_marked = false; /*printf("@@@ coeff_list tile scale=%d x=%d y=%d\n", tile->tile_scale, tile->tile_x, tile->tile_y);*/ }
    for (ITERATE_TILE_LIST(tile, children_list)) { tile->cache_marked = false; /*printf("@@@ children_list tile scale=%d x=%d y=%d\n", tile->tile_scale, tile->tile_x, tile->tile_y);*/ }

    // IO+decode: For all dependent tiles, read and decode coefficients where missing (hh, and ll for top tiles).
    // Assuming lists are sorted parents first.
    // IDWT as needed, top to bottom. This should produce idwt for this tile as well, which should be last in idwt list.
    // YCoCb->RGB for this tile only.
    uint32_t* result = NULL;
    for (ITERATE_TILE_LIST(tile, coeff_list)) {
        isyntax_openslide_load_tile_coefficients(cache, isyntax, tile);
    }
    for (ITERATE_TILE_LIST(tile, idwt_list)) {
        isyntax_openslide_load_tile_coefficients(cache, isyntax, tile);
    }
    for (ITERATE_TILE_LIST(tile, idwt_list)) {
        if (tile == idwt_list.tail) {
            isyntax_openslide_idwt(cache, isyntax, tile, pixels_buffer, pixel_format);
        } else {
            isyntax_openslide_idwt(cache, isyntax, tile, /*pixels_buffer=*/NULL, /*pixel_format=*/0);
        }
    }

    // Lock.
    // Bump all the affected tiles in cache.
    // Unmark all dependent tiles as "referenced" so that they can be evicted.
    // Perform cache trim (possibly not every invocation).
    // Unlock.

    tile_list_insert_list_first(&cache->cache_list, &children_list);
    tile_list_insert_list_first(&cache->cache_list, &coeff_list);
    tile_list_insert_list_first(&cache->cache_list, &idwt_list);

    // Cache trim. Since we have the result already, it is possible that tiles from this run will be trimmed here
    // if cache is small or work happened on other threads.
    isyntax_cache_trim(cache, cache->target_cache_size);
    benaphore_unlock(&cache->mutex);
}
