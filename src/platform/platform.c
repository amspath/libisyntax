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

#define PANIC_IMPLEMENTATION
#define STB_DS_IMPLEMENTATION
#include "common.h"

#define PLATFORM_IMPL
#include "platform.h"
#include "intrinsics.h"

#if APPLE
#include <sys/sysctl.h> // for sysctlbyname()
#endif



mem_t* platform_allocate_mem_buffer(size_t capacity) {
	size_t allocation_size = sizeof(mem_t) + capacity + 1;
	mem_t* result = (mem_t*) malloc(allocation_size);
	result->len = 0;
	result->capacity = capacity;
	return result;
}

mem_t* platform_read_entire_file(const char* filename) {
	mem_t* result = NULL;
	file_stream_t fp = file_stream_open_for_reading(filename);
	if (fp) {
		i64 filesize = file_stream_get_filesize(fp);
		if (filesize > 0) {
			size_t allocation_size = sizeof(mem_t) + filesize + 1;
			result = (mem_t*) malloc(allocation_size);
			if (result) {
				((u8*)result)[allocation_size-1] = '\0';
				result->len = filesize;
				result->capacity = filesize;
				size_t bytes_read = file_stream_read(result->data, filesize, fp);
				if (bytes_read != filesize) {
					panic();
				}
			}
		}
		file_stream_close(fp);
	}
	return result;
}


u64 file_read_at_offset(void* dest, file_stream_t fp, u64 offset, u64 num_bytes) {
	i64 prev_read_pos = file_stream_get_pos(fp);
	file_stream_set_pos(fp, offset);
	u64 result = file_stream_read(dest, num_bytes, fp);
	file_stream_set_pos(fp, prev_read_pos);
	return result;
}

bool file_exists(const char* filename) {
	return (access(filename, F_OK) != -1);
}

bool is_directory(const char* path) {
	struct stat st = {0};
	platform_stat(path, &st);
	return S_ISDIR(st.st_mode);
}


void get_system_info(bool verbose) {
#if WINDOWS
    SYSTEM_INFO system_info;
    GetSystemInfo(&system_info);
	logical_cpu_count = (i32)system_info.dwNumberOfProcessors;
	physical_cpu_count = logical_cpu_count; // TODO: how to read this on Windows?
	os_page_size = system_info.dwPageSize;
#elif APPLE
    size_t physical_cpu_count_len = sizeof(physical_cpu_count);
	size_t logical_cpu_count_len = sizeof(logical_cpu_count);
	sysctlbyname("hw.physicalcpu", &physical_cpu_count, &physical_cpu_count_len, NULL, 0);
	sysctlbyname("hw.logicalcpu", &logical_cpu_count, &logical_cpu_count_len, NULL, 0);
	os_page_size = (u32) getpagesize();
	page_alignment_mask = ~((u64)(sysconf(_SC_PAGE_SIZE) - 1));
    bool is_macos = true;
#elif LINUX
    logical_cpu_count = sysconf( _SC_NPROCESSORS_ONLN );
    physical_cpu_count = logical_cpu_count; // TODO: how to read this on Linux?
    os_page_size = (u32) getpagesize();
    page_alignment_mask = ~((u64)(sysconf(_SC_PAGE_SIZE) - 1));
#endif
    if (verbose) console_print("There are %d logical CPU cores\n", logical_cpu_count);
    total_thread_count = MIN(logical_cpu_count, MAX_THREAD_COUNT);
}


void init_thread_memory(i32 logical_thread_index) {
	// Allocate a private memory buffer
	u64 thread_memory_size = MEGABYTES(16);
	local_thread_memory = (thread_memory_t*) malloc(thread_memory_size); // how much actually needed?
	thread_memory_t* thread_memory = local_thread_memory;
	memset(thread_memory, 0, sizeof(thread_memory_t));
#if !WINDOWS
	// TODO: implement creation of async I/O events
#endif
	thread_memory->thread_memory_raw_size = thread_memory_size;

	thread_memory->aligned_rest_of_thread_memory = (void*)
			((((u64)thread_memory + sizeof(thread_memory_t) + os_page_size - 1) / os_page_size) * os_page_size); // round up to next page boundary
	thread_memory->thread_memory_usable_size = thread_memory_size - ((u64)thread_memory->aligned_rest_of_thread_memory - (u64)thread_memory);
	init_arena(&thread_memory->temp_arena, thread_memory->thread_memory_usable_size, thread_memory->aligned_rest_of_thread_memory);

}


