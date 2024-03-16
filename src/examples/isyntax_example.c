#include "libisyntax.h"

#include <stdint.h>
#include <stdio.h>
#include <assert.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb_image_write.h"  // for png export

#define CHECK_LIBISYNTAX_OK(_libisyntax_call) do { \
    isyntax_error_t result = _libisyntax_call;     \
    assert(result == LIBISYNTAX_OK);               \
} while(0)

#define LOG_VAR(fmt, var) printf("%s: %s=" fmt "\n", __FUNCTION__, #var, var)

void print_isyntax_levels(isyntax_t* isyntax) {
    const isyntax_image_t* wsi_image = libisyntax_get_wsi_image(isyntax);

    for (int i = 0; i < libisyntax_image_get_level_count(wsi_image); ++i) {
        const isyntax_level_t* level = libisyntax_image_get_level(wsi_image, i);
        LOG_VAR("%d", i);
        LOG_VAR("%d", libisyntax_level_get_scale(level));
        LOG_VAR("%d", libisyntax_level_get_width_in_tiles(level));
        LOG_VAR("%d", libisyntax_level_get_height_in_tiles(level));
    }
}

int main(int argc, char** argv) {

    if (argc <= 1) {
        printf("Usage: %s <isyntax_file> - show levels & tiles.\n"
               "       %s <isyntax_file> <level> <tile_x> <tile_y> <output.png> - write a tile to output.png\n"
               "       %s <isyntax_file> label <output.jpg> - write label image to output.jpg\n"
               "       %s <isyntax_file> macro <output.jpg> - write macro image to output.jpg\n",
               argv[0], argv[0], argv[0], argv[0]);
        return 0;
    }

    char* filename = argv[1];

    libisyntax_init();

    isyntax_t* isyntax;
    if (libisyntax_open(filename, /*is_init_allocators=*/0, &isyntax) != LIBISYNTAX_OK) {
        printf("Failed to open %s\n", filename);
        return -1;
    }
    printf("Successfully opened %s\n", filename);

    if (argc >= 6) {
        int level = atoi(argv[2]);
        int tile_x = atoi(argv[3]);
        int tile_y = atoi(argv[4]);
        const char* output_png = argv[5];

        LOG_VAR("%d", level);
        LOG_VAR("%d", tile_x);
        LOG_VAR("%d", tile_y);
        LOG_VAR("%s", output_png);

        int32_t tile_width = libisyntax_get_tile_width(isyntax);
        int32_t tile_height = libisyntax_get_tile_height(isyntax);
        LOG_VAR("%d", tile_width);
        LOG_VAR("%d", tile_height);

        isyntax_cache_t *isyntax_cache = NULL;
        CHECK_LIBISYNTAX_OK(libisyntax_cache_create("example cache", 2000, &isyntax_cache));
        CHECK_LIBISYNTAX_OK(libisyntax_cache_inject(isyntax_cache, isyntax));

        // RGBA is what stbi expects.
        uint32_t *pixels_rgba = malloc(tile_width * tile_height * 4);
        CHECK_LIBISYNTAX_OK(libisyntax_tile_read(isyntax, isyntax_cache, level, tile_x, tile_y,
                                                 pixels_rgba, LIBISYNTAX_PIXEL_FORMAT_RGBA));

        printf("Writing %s...\n", output_png);
        stbi_write_png(output_png, tile_width, tile_height, 4, pixels_rgba, tile_width * 4);
        printf("Done writing %s.\n", output_png);

        free(pixels_rgba);
        libisyntax_cache_destroy(isyntax_cache);

    } else if (argc >= 4) {

        if (strcmp(argv[2], "label") == 0 || strcmp(argv[2], "macro") == 0) {

            const char* output_jpg = argv[3];
            LOG_VAR("%s", output_jpg);

            uint8_t* jpeg_buffer = NULL;
            uint32_t jpeg_size = 0;
            if (strcmp(argv[2], "label") == 0) {
                CHECK_LIBISYNTAX_OK(libisyntax_read_label_image_jpeg(isyntax, &jpeg_buffer, &jpeg_size));
            } else if (strcmp(argv[2], "macro") == 0) {
                CHECK_LIBISYNTAX_OK(libisyntax_read_macro_image_jpeg(isyntax, &jpeg_buffer, &jpeg_size));
            }

            if (jpeg_buffer) {
                FILE* fp = fopen(output_jpg, "wb");
                if (fp) {
                    fwrite(jpeg_buffer, jpeg_size, 1, fp);
                    fclose(fp);
                }
                free(jpeg_buffer);
            }

        } else {
            print_isyntax_levels(isyntax);
        }
    } else {
        print_isyntax_levels(isyntax);
    }

    libisyntax_close(isyntax);
    return 0;
}
