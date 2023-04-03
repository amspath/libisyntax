#include "common.h"
#include "isyntax/isyntax_reader.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb_image_write.h"

// TODO(avirodov): libisyntax_init()? not sure who has to call the per-thread init, need discussion.
#include "platform/platform.h"  // for get_system_info(..), init_thread_memory(..)


#define LOG_VAR(fmt, var) printf("%s: %s=" fmt "\n", __FUNCTION__, #var, var)

uint32_t bgra_to_rgba(uint32_t val) {
    return ((val & 0xff) << 16) | (val & 0x00ff00) | ((val & 0xff0000) >> 16) | (val & 0xff000000);
}

void print_isyntax_levels(isyntax_t* isyntax) {
    int wsi_image_idx = isyntax->wsi_image_index;
    LOG_VAR("%d", wsi_image_idx);
    isyntax_image_t* wsi_image = &isyntax->images[wsi_image_idx];
    isyntax_level_t* levels = wsi_image->levels;

    for (int i = 0; i < wsi_image->level_count; ++i) {
        LOG_VAR("%d", i);
        LOG_VAR("%d", levels[i].scale);
        LOG_VAR("%d", levels[i].width_in_tiles);
        LOG_VAR("%d", levels[i].height_in_tiles);
    }
}

int main(int argc, char** argv) {

	if (argc <= 1) {
        printf("Usage: %s <isyntax_file> - show levels & tiles.\n"
               "       %s <isyntax_file> <level> <tile_x> <tile_y> <output.png> - write a tile to output.png",
               argv[0], argv[0]);
		return 0;
	}

	char* filename = argv[1];

    get_system_info(/*verbose=*/true);
    init_thread_memory(0);

	isyntax_t isyntax = {0};
	if (!isyntax_open(&isyntax, filename, /*init_allocators=*/false)) {
        printf("Failed to open %s\n", filename);
        return -1;
    }
    printf("Successfully opened %s\n", filename);

    if (argc <= 5) {
        print_isyntax_levels(&isyntax);
    } else {
        int level = atoi(argv[2]);
        int tile_x = atoi(argv[3]);
        int tile_y = atoi(argv[4]);
        const char* output_png = argv[5];

        LOG_VAR("%d", level);
        LOG_VAR("%d", tile_x);
        LOG_VAR("%d", tile_y);
        LOG_VAR("%s", output_png);

        isyntax_cache_t *isyntax_cache = isyntax_make_cache("example_cache", 2000,
                                                            isyntax.block_width, isyntax.block_height);
        // Allocators must be null because we asked to not init_allocators in isyntax_open().
        assert(isyntax.h_coeff_block_allocator == NULL);
        assert(isyntax.ll_coeff_block_allocator == NULL);
        isyntax.ll_coeff_block_allocator = &isyntax_cache->ll_coeff_block_allocator;
        isyntax.h_coeff_block_allocator = &isyntax_cache->h_coeff_block_allocator;
        isyntax.is_block_allocator_owned = false;

        uint32_t *pixels = isyntax_read_tile_bgra(isyntax_cache, &isyntax, level, tile_x, tile_y);

        // convert data to the correct pixel format (bgra->rgba).
        for (int i = 0; i < isyntax.tile_height * isyntax.tile_width; ++i) {
            pixels[i] = bgra_to_rgba(pixels[i]);
        }
        printf("Writing %s...\n", output_png);
        stbi_write_png(output_png, isyntax.tile_width, isyntax.tile_height, 4, pixels, isyntax.tile_width * 4);
        printf("Done writing %s.\n", output_png);

        isyntax_destroy_and_free_cache(isyntax_cache);
    }

    isyntax_destroy(&isyntax);
	return 0;
}
