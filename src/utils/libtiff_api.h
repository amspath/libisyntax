/*
  BSD 2-Clause License

  Copyright (c) 2019-2024, Pieter Valkema

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

// Dynamic binding for libtiff.
// Useful for avoiding statically linking the libtiff library, which is a headache on Windows (at least for me).

#pragma once
#ifndef LIBTIFF_API_H
#define LIBTIFF_API_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>

#if __has_include("tiffio.h")
// We don't actually need this to compile the dynamic binding.
// However, we still want access to the libtiff definitions for the rest of the code.
#include "tiffio.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tiff TIFF;

#if !defined(WINAPI)
#if defined(_WIN32)
#include <windows.h>
#else
#define WINAPI
#endif
#endif


// Prototypes
bool init_libtiff_at_runtime();



// Globals
#if defined(LIBTIFF_API_IMPL)
#define INIT(...) __VA_ARGS__
#define extern
#else
#define INIT(...)
#undef extern
#endif

#define TIFFOpen libtiff_TIFFOpen
#define TIFFSetField libtiff_TIFFSetField
#define TIFFWriteDirectory libtiff_TIFFWriteDirectory
#define TIFFClose libtiff_TIFFClose
#define TIFFWriteTile libtiff_TIFFWriteTile

extern TIFF *  (WINAPI *libtiff_TIFFOpen)(const char *name, const char *mode);
extern int     (WINAPI *libtiff_TIFFSetField)(TIFF *tif, uint32_t tag, ...);
extern int     (WINAPI *libtiff_TIFFWriteDirectory)(TIFF *tif);
extern void    (WINAPI *libtiff_TIFFClose)(TIFF *tif);
extern ssize_t (WINAPI *libtiff_TIFFWriteTile)(TIFF *tif, void *buf, uint32_t x, uint32_t y, uint32_t z, uint16_t s);


#undef INIT
#undef extern


#if defined(LIBTIFF_API_IMPL)

// Implementation

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#ifndef COUNT
#define COUNT(array) (sizeof(array) / sizeof((array)[0]))
#endif

#ifdef _WIN32
static inline const wchar_t* one_past_last_slash_w(const wchar_t* s, int max) {
	if (max <= 0) return s;
	size_t len = wcsnlen(s, (size_t)(max - 1));
	size_t stripped_len = 0;
	const wchar_t* pos = s + len - 1;
	for (; pos >= s; --pos) {
		wchar_t c = *pos;
		if (c == '/' || c == '\\')  {
			break;
		} else {
			++stripped_len;
		}
	}
	const wchar_t* result = pos + 1; // gone back one too far
	assert(stripped_len > 0 && stripped_len <= len);
	return result;
}
#endif

bool init_libtiff_at_runtime() {
#ifdef _WIN32
	// Look for DLLs in a libtiff/ folder in the same location as the .exe
	wchar_t dll_path[4096];
	GetModuleFileNameW(NULL, dll_path, sizeof(dll_path));
	wchar_t* pos = (wchar_t*)one_past_last_slash_w(dll_path, sizeof(dll_path));
	int chars_left = COUNT(dll_path) - (pos - dll_path);
	wcsncpy(pos, L"libtiff", chars_left);
	SetDllDirectoryW(dll_path);

	HINSTANCE library_handle = LoadLibraryW(L"libtiff-6.dll");
	if (!library_handle) {
		// If DLL not found in the libtiff/ folder, look one folder up (in the same location as the .exe)
		*pos = '\0';
		SetDllDirectoryW(dll_path);
		library_handle = LoadLibraryW(L"libtiff-6.dll");
	}
	SetDllDirectoryW(NULL);

#elif defined(__APPLE__)
	void* library_handle = dlopen("libtiff.dylib", RTLD_LAZY);
	if (!library_handle) {
		// Check expected library path for MacPorts
		library_handle = dlopen("/opt/local/lib/libtiff.dylib", RTLD_LAZY);
		if (!library_handle) {
			// Check expected library path for Homebrew
			library_handle = dlopen("/usr/local/opt/libtiff/lib/libtiff.dylib", RTLD_LAZY);
		}
	}
#else
	void* library_handle = dlopen("libtiff.so", RTLD_LAZY);
	if (!library_handle) {
		library_handle = dlopen("/usr/local/lib/libtiff.so", RTLD_LAZY);
	}
#endif

	bool success = false;
	if (library_handle) {

#ifdef _WIN32
#define GET_PROC(proc) if (!(libtiff_##proc = (void*) GetProcAddress(library_handle, #proc))) goto failed;
#else
#define GET_PROC(proc) if (!(libtiff.proc = (void*) dlsym(library_handle, #proc))) goto failed;
#endif
		GET_PROC(TIFFOpen);
		GET_PROC(TIFFSetField);
		GET_PROC(TIFFWriteDirectory);
		GET_PROC(TIFFClose);
		GET_PROC(TIFFWriteTile);
#undef GET_PROC

		success = true;

	} else failed: {
#ifdef _WIN32
		//win32_diagnostic("LoadLibraryA");
		fprintf(stderr, "LibTIFF not available: could not load libtiff-6.dll\n");
#elif defined(__APPLE__)
		fprintf(stderr, "LibTIFF not available: could not load libtiff.dylib (not installed?)\n");
#else
		fprintf(stderr, "LibTIFF not available: could not load libtiff.so (not installed?)\n");
#endif
		success = false;
	}
	return success;
}

#endif // LIBTIFF_API_IMPL


#ifdef __cplusplus
}
#endif


#endif //LIBTIFF_API_H

