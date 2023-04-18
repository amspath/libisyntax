/*
  BSD 2-Clause License

  Copyright (c) 2019-2023, Pieter Valkema

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

#define STB_SPRINTF_IMPLEMENTATION
#include "stb_sprintf.h"

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include "common.h"
#include "platform.h"
#include "intrinsics.h"

#include "libisyntax.h"
#include "isyntax.h"
#include "isyntax_reader.h"

static platform_thread_info_t thread_infos[MAX_THREAD_COUNT];



// Routines for initializing the global thread pool

#if WINDOWS
#include "win32_utils.h"

_Noreturn DWORD WINAPI thread_proc(void* parameter) {
	platform_thread_info_t* thread_info = (platform_thread_info_t*) parameter;
	i64 init_start_time = get_clock();

	atomic_increment(&global_worker_thread_idle_count);

	init_thread_memory(thread_info->logical_thread_index, &global_system_info);
	thread_memory_t* thread_memory = local_thread_memory;

	for (i32 i = 0; i < MAX_ASYNC_IO_EVENTS; ++i) {
		thread_memory->async_io_events[i] = CreateEventA(NULL, TRUE, FALSE, NULL);
		if (!thread_memory->async_io_events[i]) {
			win32_diagnostic("CreateEvent");
		}
	}
//	console_print("Thread %d reporting for duty (init took %.3f seconds)\n", thread_info->logical_thread_index, get_seconds_elapsed(init_start_time, get_clock()));

	for (;;) {
		if (thread_info->logical_thread_index > global_active_worker_thread_count) {
			// Worker is disabled, do nothing
			Sleep(100);
			continue;
		}
		if (!is_queue_work_in_progress(thread_info->queue)) {
			Sleep(1);
			WaitForSingleObjectEx(thread_info->queue->semaphore, 1, FALSE);
		}
		do_worker_work(thread_info->queue, thread_info->logical_thread_index);
	}
}

static void init_thread_pool() {
	init_thread_memory(0, &global_system_info);

	global_worker_thread_count = total_thread_count - 1;
	global_active_worker_thread_count = global_worker_thread_count;

	global_work_queue = create_work_queue("/worksem", 1024); // Queue for newly submitted tasks
	global_completion_queue = create_work_queue("/completionsem", 1024); // Message queue for completed tasks

	// NOTE: the main thread is considered thread 0.
	for (i32 i = 1; i < total_thread_count; ++i) {
		platform_thread_info_t thread_info = { .logical_thread_index = i, .queue = &global_work_queue};
		thread_infos[i] = thread_info;

		DWORD thread_id;
		HANDLE thread_handle = CreateThread(NULL, 0, thread_proc, thread_infos + i, 0, &thread_id);
		CloseHandle(thread_handle);

	}


}

#else

#include <pthread.h>

static void* worker_thread(void* parameter) {
    platform_thread_info_t* thread_info = (platform_thread_info_t*) parameter;

//	fprintf(stderr, "Hello from thread %d\n", thread_info->logical_thread_index);

    init_thread_memory(thread_info->logical_thread_index, &global_system_info);
	atomic_increment(&global_worker_thread_idle_count);

	for (;;) {
		if (thread_info->logical_thread_index > global_active_worker_thread_count) {
			// Worker is disabled, do nothing
			platform_sleep(100);
			continue;
		}
        if (!is_queue_work_waiting_to_start(thread_info->queue)) {
            //platform_sleep(1);
            sem_wait(thread_info->queue->semaphore);
            if (thread_info->logical_thread_index > global_active_worker_thread_count) {
                // Worker is disabled, do nothing
                platform_sleep(100);
                continue;
            }
        }
        do_worker_work(thread_info->queue, thread_info->logical_thread_index);
    }

    return 0;
}

static void init_thread_pool() {
	init_thread_memory(0, &global_system_info);
    global_worker_thread_count = global_system_info.suggested_total_thread_count - 1;
    global_active_worker_thread_count = global_worker_thread_count;

	global_work_queue = create_work_queue("/worksem", 1024); // Queue for newly submitted tasks
	global_completion_queue = create_work_queue("/completionsem", 1024); // Message queue for completed tasks

    pthread_t threads[MAX_THREAD_COUNT] = {};

    // NOTE: the main thread is considered thread 0.
    for (i32 i = 1; i < global_system_info.suggested_total_thread_count; ++i) {
        thread_infos[i] = (platform_thread_info_t){ .logical_thread_index = i, .queue = &global_work_queue};

        if (pthread_create(threads + i, NULL, &worker_thread, (void*)(&thread_infos[i])) != 0) {
            fprintf(stderr, "Error creating thread\n");
        }

    }

    test_multithreading_work_queue();


}

#endif


isyntax_error_t libisyntax_init() {
	get_system_info(false);
	init_thread_pool();
    return LIBISYNTAX_OK;
}

isyntax_error_t libisyntax_open(const char* filename, int32_t is_init_allocators, isyntax_t** out_isyntax) {
    // Note(avirodov): intentionally not changing api of isyntax_open. We can do that later if needed and reduce
    // the size/count of wrappers.
    isyntax_t* result = malloc(sizeof(isyntax_t));
    memset(result, 0, sizeof(*result));

    bool success = isyntax_open(result, filename, is_init_allocators);
    if (success) {
        *out_isyntax = result;
        return LIBISYNTAX_OK;
    } else {
        free(result);
        return LIBISYNTAX_FATAL;
    }
}

void libisyntax_close(isyntax_t* isyntax) {
    isyntax_destroy(isyntax);
    free(isyntax);
}

int32_t libisyntax_get_tile_width(const isyntax_t* isyntax) {
    return isyntax->tile_width;
}

int32_t libisyntax_get_tile_height(const isyntax_t* isyntax) {
    return isyntax->tile_height;
}

int32_t libisyntax_get_wsi_image_index(const isyntax_t* isyntax) {
    return isyntax->wsi_image_index;
}

const isyntax_image_t* libisyntax_get_image(const isyntax_t* isyntax, int32_t wsi_image_index) {
    return &isyntax->images[wsi_image_index];
}

int32_t libisyntax_image_get_level_count(const isyntax_image_t* image) {
    return image->level_count;
}

const isyntax_level_t* libisyntax_image_get_level(const isyntax_image_t* image, int32_t index) {
    return &image->levels[index];
}

int32_t libisyntax_level_get_scale(const isyntax_level_t* level) {
    return level->scale;
}

int32_t libisyntax_level_get_width_in_tiles(const isyntax_level_t* level) {
    return level->width_in_tiles;
}

int32_t libisyntax_level_get_height_in_tiles(const isyntax_level_t* level) {
    return level->height_in_tiles;
}

isyntax_error_t libisyntax_cache_create(const char* debug_name_or_null, int32_t cache_size,
                                        isyntax_cache_t** out_isyntax_cache)
{
    isyntax_cache_t* cache_ptr = malloc(sizeof(isyntax_cache_t));
    memset(cache_ptr, 0, sizeof(*cache_ptr));
    tile_list_init(&cache_ptr->cache_list, debug_name_or_null);
    cache_ptr->target_cache_size = cache_size;
    cache_ptr->mutex = benaphore_create();

    // Note: rest of initialization is deferred to the first injection, as that is where we will know the block size.

    *out_isyntax_cache = cache_ptr;
    return LIBISYNTAX_OK;
}

isyntax_error_t libisyntax_cache_inject(isyntax_cache_t* isyntax_cache, isyntax_t* isyntax) {
    // TODO(avirodov): consider refactoring implementation to another file, here and in destroy.
    if (isyntax->ll_coeff_block_allocator != NULL || isyntax->h_coeff_block_allocator != NULL) {
        return LIBISYNTAX_INVALID_ARGUMENT;
    }

    if (!isyntax_cache->h_coeff_block_allocator.is_valid || !isyntax_cache->ll_coeff_block_allocator.is_valid) {
        // Shouldn't ever partially initialize.
        assert(!isyntax_cache->h_coeff_block_allocator.is_valid);
        assert(!isyntax_cache->ll_coeff_block_allocator.is_valid);

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

    if (isyntax_cache->allocator_block_width != isyntax->block_width ||
            isyntax_cache->allocator_block_height != isyntax->block_height) {
        return LIBISYNTAX_FATAL; // Not implemented, see todo in libisyntax.h.
    }

    isyntax->ll_coeff_block_allocator = &isyntax_cache->ll_coeff_block_allocator;
    isyntax->h_coeff_block_allocator = &isyntax_cache->h_coeff_block_allocator;
    isyntax->is_block_allocator_owned = false;
    return LIBISYNTAX_OK;
}

void libisyntax_cache_destroy(isyntax_cache_t* isyntax_cache) {
    if (isyntax_cache->ll_coeff_block_allocator.is_valid) {
        block_allocator_destroy(&isyntax_cache->ll_coeff_block_allocator);
    }
    if (isyntax_cache->h_coeff_block_allocator.is_valid) {
        block_allocator_destroy(&isyntax_cache->h_coeff_block_allocator);
    }
    benaphore_destroy(&isyntax_cache->mutex);
    free(isyntax_cache);
}

isyntax_error_t libisyntax_tile_read(isyntax_t* isyntax, isyntax_cache_t* isyntax_cache,
                                     int32_t level, int64_t tile_x, int64_t tile_y, uint32_t** out_pixels) {
    // TODO(avirodov): if isyntax_cache is null, we can support using allocators that are in isyntax object,
    //  if is_init_allocators = 1 when created. Not sure is needed.
    *out_pixels = isyntax_read_tile_bgra(isyntax, isyntax_cache, level, tile_x, tile_y);
    return LIBISYNTAX_OK;
}

isyntax_error_t libisyntax_read_region(isyntax_t* isyntax, isyntax_cache_t* isyntax_cache, int32_t level,
                                       int64_t x, int64_t y, int64_t width, int64_t height, uint32_t** out_pixels) {

    // Compute the offset
    // TODO: Is this always in [0]?
    v2f offset = isyntax->images[0].levels[level].origin_offset;
    x += (int64_t) (offset.x * isyntax->mpp_x);
    y += (int64_t) (offset.y * isyntax->mpp_y);

    // Calculate tile coordinates
    int32_t tile_width = isyntax->tile_width;
    int32_t tile_height = isyntax->tile_height;

    int64_t start_tile_x = x / tile_width;
    int64_t end_tile_x = (x + width - 1) / tile_width;
    int64_t start_tile_y = y / tile_height;
    int64_t end_tile_y = (y + height - 1) / tile_height;

    // Allocate memory for region
    *out_pixels = (uint32_t*)malloc(width * height * sizeof(uint32_t));

    // Read tiles and copy the relevant portion of each tile to the region
    for (int64_t tile_y = start_tile_y; tile_y <= end_tile_y; ++tile_y) {
        for (int64_t tile_x = start_tile_x; tile_x <= end_tile_x; ++tile_x) {

            // Read tile
            uint32_t* pixels = NULL;
            assert(libisyntax_tile_read(isyntax, isyntax_cache, level, tile_x, tile_y, &pixels) == LIBISYNTAX_OK);

            // Calculate the portion of the tile to be copied
            int64_t src_x = (tile_x == start_tile_x) ? x % tile_width : 0;
            int64_t src_y = (tile_y == start_tile_y) ? y % tile_height : 0;
            int64_t dest_x = (tile_x == start_tile_x) ? 0 : (tile_x - start_tile_x) * tile_width - (x % tile_width);
            int64_t dest_y = (tile_y == start_tile_y) ? 0 : (tile_y - start_tile_y) * tile_height - (y % tile_height);
            int64_t copy_width = (tile_x == end_tile_x) ? (x + width) % tile_width : tile_width - src_x;
            int64_t copy_height = (tile_y == end_tile_y) ? (y + height) % tile_height : tile_height - src_y;

            // Copy the relevant portion of the tile to the region
            for (int64_t i = 0; i < copy_height; ++i) {
                memcpy((*out_pixels) + (dest_y + i) * width + dest_x,
                       pixels + (src_y + i) * tile_width + src_x,
                       copy_width * sizeof(uint32_t));
            }



            // Free the tile data
            free(pixels);
        }
    }

    return LIBISYNTAX_OK;
}


void libisyntax_tile_free_pixels(uint32_t* pixels) {
    free(pixels);
}


