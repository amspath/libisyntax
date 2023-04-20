#include "libisyntax.h"
#include "tiffio.h"
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define LOG_VAR(fmt, var) printf("%s: %s=" fmt "\n", __FUNCTION__, #var, var)

void update_progress(int32_t total_progress, int32_t page_progress, int32_t page_number) {
    printf("\rProgress: %3d%% | Page %d progress: %3d%%", total_progress, page_number, page_progress);
    fflush(stdout);
}
void write_page_to_tiff(TIFF *output_tiff, isyntax_t *isyntax, isyntax_cache_t *isyntax_cache, isyntax_level_t* level, int32_t tile_width, int32_t tile_height, int32_t total_tiles_written) {
    int32_t width = libisyntax_level_get_width(level);
    int32_t height = libisyntax_level_get_height(level);

    // Set the TIFF properties for the current level.
    TIFFSetField(output_tiff, TIFFTAG_IMAGEWIDTH, width);
    TIFFSetField(output_tiff, TIFFTAG_IMAGELENGTH, height);
    TIFFSetField(output_tiff, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(output_tiff, TIFFTAG_SAMPLESPERPIXEL, 4);
    TIFFSetField(output_tiff, TIFFTAG_COMPRESSION, COMPRESSION_LZW);
    TIFFSetField(output_tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
    TIFFSetField(output_tiff, TIFFTAG_ORIENTATION, ORIENTATION_TOPLEFT);
    TIFFSetField(output_tiff, TIFFTAG_PLANARCONFIG, PLANARCONFIG_CONTIG);
    TIFFSetField(output_tiff, TIFFTAG_TILEWIDTH, tile_width);
    TIFFSetField(output_tiff, TIFFTAG_TILELENGTH, tile_height);

    // Set the resolution
    double res_x = 10000.0 / libisyntax_level_get_mpp_x(level);
    TIFFSetField(output_tiff, TIFFTAG_XRESOLUTION, res_x);
    double res_y = 10000.0 / libisyntax_level_get_mpp_y(level);
    TIFFSetField(output_tiff, TIFFTAG_YRESOLUTION, res_y);
    TIFFSetField(output_tiff, TIFFTAG_RESOLUTIONUNIT, RESUNIT_CENTIMETER);
    TIFFSetField(output_tiff, TIFFTAG_EXTRASAMPLES, 1, (uint16_t[]){EXTRASAMPLE_UNASSALPHA});


    if (level == 0) {
        TIFFSetField(output_tiff, TIFFTAG_SUBFILETYPE, 0);
    } else {
        TIFFSetField(output_tiff, TIFFTAG_SUBFILETYPE, FILETYPE_REDUCEDIMAGE);
    }

    int32_t total_tiles = (height / tile_height) * (width / tile_width);
    int32_t tile_progress = 0;

    int32_t scale = libisyntax_level_get_scale(level);

    int32_t tiles_in_page = height * width / tile_height / tile_width;
    // Write the current level tile by tile.
    for (int32_t y_coord = 0; y_coord < height; y_coord += tile_height) {
        for (int32_t x_coord = 0; x_coord < width; x_coord += tile_width) {
            // At the borders the tile size can be smaller
            int32_t region_width = (x_coord + tile_width > width) ? width - x_coord : tile_width;
            int32_t region_height = (y_coord + tile_height > height) ? height - y_coord : tile_height;

            uint32_t *pixels = NULL;
            assert(libisyntax_read_region(isyntax, isyntax_cache, scale, x_coord, y_coord, region_width, region_height, &pixels) == LIBISYNTAX_OK);

            // Convert data to the correct pixel format (bgra->rgba).
            bgra_to_rgba(pixels, region_width, region_height);

            uint32_t *tile_pixels = pixels;

            if (region_width != tile_width || region_height != tile_height) {
                tile_pixels = calloc(tile_width * tile_height, sizeof(uint32_t));
                for (int32_t row = 0; row < region_height; ++row) {
                    memcpy(tile_pixels + row * tile_width, pixels + row * region_width, region_width * sizeof(uint32_t));
                }
            }

            // Write the tile to the output TIFF.
            TIFFWriteTile(output_tiff, tile_pixels, x_coord, y_coord, 0, 0);

            ++tile_progress;
            int32_t tile_percent = (tile_progress * 100) / tiles_in_page;
            update_progress(total_tiles_written, tile_percent, scale);

            libisyntax_tile_free_pixels(pixels);
        }
    }

    // Write the directory for the current level.
    TIFFWriteDirectory(output_tiff);
}



int main(int argc, char **argv) {
    if (argc <= 2) {
        printf("Usage: %s <isyntax_file> <output.tif> - convert iSyntax file to a pyramidal BigTIFF\n", argv[0]);
        return 0;
    }

    char *filename = argv[1];
    char *output_tiffname = argv[2];

    libisyntax_init();

    isyntax_t *isyntax;
    if (libisyntax_open(filename, /*is_init_allocators=*/0, &isyntax) != LIBISYNTAX_OK) {
        printf("Failed to open %s\n", filename);
        return -1;
    }
    printf("Successfully opened %s\n", filename);

    int32_t tile_width = libisyntax_get_tile_width(isyntax);
    int32_t tile_height = libisyntax_get_tile_height(isyntax);
    LOG_VAR("%d", tile_width);
    LOG_VAR("%d", tile_height);

    isyntax_cache_t *isyntax_cache = NULL;
    assert(libisyntax_cache_create("example cache", 2000, &isyntax_cache) == LIBISYNTAX_OK);
    assert(libisyntax_cache_inject(isyntax_cache, isyntax) == LIBISYNTAX_OK);

    // Initialize the output TIFF file.
    TIFF *output_tiff;
    output_tiff = TIFFOpen(output_tiffname, "w8");
    if (!output_tiff) {
        printf("Failed to create %s\n", output_tiffname);
        return -1;
    }
    // TODO: Tile size != 256, 256
    tile_width = 1024;
    tile_height = 1024;

    // Write all levels to the output TIFF.
    int start_at_page = 3;

    const isyntax_image_t *image = libisyntax_get_image(isyntax, 0);
    int32_t num_levels = libisyntax_image_get_level_count(image);
    int32_t total_tiles = 0;

    // Let's find the total number of tiles so we can have a progress counter
    for (int32_t level = start_at_page; level < num_levels; ++level) {
        isyntax_level_t* current_level = libisyntax_image_get_level(image, level);
        int32_t width = libisyntax_level_get_width(current_level);
        int32_t height = libisyntax_level_get_height(current_level);
        int32_t tiles_in_page = ((height - 1) / tile_height) * ((width - 1) / tile_width) + 2;
        total_tiles += tiles_in_page;
    }

    int32_t total_tiles_written = 0;
    for (int32_t level = start_at_page; level < num_levels; ++level) {
        isyntax_level_t* current_level = libisyntax_image_get_level(image, level);
        int32_t width = libisyntax_level_get_width(current_level);
        int32_t height = libisyntax_level_get_height(current_level);
        int32_t tiles_in_page = (height / tile_height) * (width / tile_width);

        write_page_to_tiff(output_tiff, isyntax, isyntax_cache, current_level, tile_width, tile_height, total_tiles_written);

        total_tiles_written += tiles_in_page;
        int32_t total_progress = (total_tiles_written * 100) / total_tiles;
        int32_t page_progress = (total_tiles_written * 100) / tiles_in_page;
        update_progress(total_progress, 0, level); // Reset tile progress to 0
    }

    // Close the output TIFF file.
    TIFFClose(output_tiff);

    // Clean up.
    libisyntax_cache_destroy(isyntax_cache);
    libisyntax_close(isyntax);

    return 0;
}
