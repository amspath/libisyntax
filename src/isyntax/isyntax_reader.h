#pragma once

#include "isyntax.h"
#include "libisyntax.h"
#include "benaphore.h"

// TODO(avirodov): can this ever fail?
void isyntax_tile_read(isyntax_t* isyntax, int scale, int tile_x, int tile_y,
                       uint32_t* pixels_buffer, enum isyntax_pixel_format_t pixel_format);

void tile_list_init(isyntax_tile_list_t* list, const char* dbg_name);
void tile_list_remove(isyntax_tile_list_t* list, isyntax_tile_t* tile);

