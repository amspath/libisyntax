#include "libisyntax.h"
#include <stdint.h>
#include <assert.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "third_party/stb_image_write.h"  // for png export

#define LOG_VAR(fmt, var) printf("%s: %s=" fmt "\n", __FUNCTION__, #var, var)


int main(int argc, char **argv) {

    if (argc <= 7) {
        printf("Usage: %s <isyntax_file> <level> <x_coord> <y_coord> <width> <height> <output.png> - write a tile to output.png",
               argv[0], argv[0]);
        return 0;
    }

    char *filename = argv[1];

    libisyntax_init();

    isyntax_t *isyntax;
    if (libisyntax_open(filename, /*is_init_allocators=*/0, &isyntax) != LIBISYNTAX_OK) {
        printf("Failed to open %s\n", filename);
        return -1;
    }
    printf("Successfully opened %s\n", filename);

    int32_t level = atoi(argv[2]);
    int32_t x_coord = atoi(argv[3]);
    int32_t y_coord = atoi(argv[4]);
    int32_t region_width = atoi(argv[5]);
    int32_t region_height = atoi(argv[6]);
    const char *output_png = argv[7];

    LOG_VAR("%d", level);
    LOG_VAR("%d", x_coord);
    LOG_VAR("%d", y_coord);
    LOG_VAR("%d", region_width);
    LOG_VAR("%d", region_height);
    LOG_VAR("%s", output_png);

    int32_t tile_width = libisyntax_get_tile_width(isyntax);
    int32_t tile_height = libisyntax_get_tile_height(isyntax);
    LOG_VAR("%d", tile_width);
    LOG_VAR("%d", tile_height);

    isyntax_cache_t *isyntax_cache = NULL;
    assert(libisyntax_cache_create("example cache", 2000, &isyntax_cache) == LIBISYNTAX_OK);
    assert(libisyntax_cache_inject(isyntax_cache, isyntax) == LIBISYNTAX_OK);

    uint32_t *pixels = NULL;
    assert(libisyntax_read_region(isyntax, isyntax_cache, level, x_coord, y_coord, region_width, region_height, &pixels) ==
           LIBISYNTAX_OK);

    // convert data to the correct pixel format (bgra->rgba).
    bgra_to_rgba(pixels, region_width, region_height);

    printf("Writing %s...\n", output_png);
    stbi_write_png(output_png, region_width, region_height, 4, pixels, region_width * 4);
    printf("Done writing %s.\n", output_png);

    libisyntax_tile_free_pixels(pixels);
    libisyntax_cache_destroy(isyntax_cache);
    libisyntax_close(isyntax);
    return 0;
}
