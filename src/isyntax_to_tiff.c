#include "libisyntax.h"
#include "tiffio.h"
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


#define LOG_VAR(fmt, var) printf("%s: %s=" fmt "\n", __FUNCTION__, #var, var)

void update_progress(int32_t total_progress, int32_t page_progress, int32_t page_number, double eta) {
    printf("\rProgress: %3d%% | Page %d progress: %3d%% | ETA: %.0fs", total_progress, page_number, page_progress, eta);
    fflush(stdout);
}


void write_page_to_tiff(TIFF *output_tiff, isyntax_t *isyntax, isyntax_cache_t *isyntax_cache, isyntax_level_t *level,
                        int32_t tile_width, int32_t tile_height, int32_t *total_tiles_written, int32_t total_tiles,
                        clock_t global_start_time, uint16_t compression_type, uint16_t quality) {
    int32_t width = libisyntax_level_get_width(level);
    int32_t height = libisyntax_level_get_height(level);
    int32_t scale = libisyntax_level_get_scale(level);

    // Set the TIFF properties for the current level.
    TIFFSetField(output_tiff, TIFFTAG_IMAGEWIDTH, width);
    TIFFSetField(output_tiff, TIFFTAG_IMAGELENGTH, height);
    TIFFSetField(output_tiff, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(output_tiff, TIFFTAG_SAMPLESPERPIXEL, 4);

    if (compression_type == COMPRESSION_JPEG) {
        TIFFSetField(output_tiff, TIFFTAG_COMPRESSION, COMPRESSION_JPEG);
        TIFFSetField(output_tiff, TIFFTAG_JPEGQUALITY, quality);
    } else if (compression_type == COMPRESSION_LZW) {
        TIFFSetField(output_tiff, TIFFTAG_COMPRESSION, COMPRESSION_LZW);
    }

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
    TIFFSetField(output_tiff, TIFFTAG_EXTRASAMPLES, 1, (uint16_t[]) {EXTRASAMPLE_UNASSALPHA});

    if (level == 0) {
        TIFFSetField(output_tiff, TIFFTAG_SUBFILETYPE, 0);
    } else {
        TIFFSetField(output_tiff, TIFFTAG_SUBFILETYPE, FILETYPE_REDUCEDIMAGE);
    }

    int32_t tile_progress = 0;
    int32_t tiles_in_page = ((height - 1) / tile_height) * ((width - 1) / tile_width) + 2;

    for (int32_t y_coord = 0; y_coord < height; y_coord += tile_height) {
        for (int32_t x_coord = 0; x_coord < width; x_coord += tile_width) {
            // At the borders the tile size can be smaller
            int32_t region_width = (x_coord + tile_width > width) ? width - x_coord : tile_width;
            int32_t region_height = (y_coord + tile_height > height) ? height - y_coord : tile_height;

            uint32_t *pixels = NULL;
            assert(libisyntax_read_region(isyntax, isyntax_cache, scale, x_coord, y_coord, region_width, region_height,
                                          &pixels) == LIBISYNTAX_OK);

            // Convert data to the correct pixel format (bgra->rgba).
            bgra_to_rgba(pixels, region_width, region_height);

            uint32_t *tile_pixels = pixels;

            if (region_width != tile_width || region_height != tile_height) {
                tile_pixels = calloc(tile_width * tile_height, sizeof(uint32_t));
                for (int32_t row = 0; row < region_height; ++row) {
                    memcpy(tile_pixels + row * tile_width, pixels + row * region_width,
                           region_width * sizeof(uint32_t));
                }
            }

            // Write the tile to the output TIFF.
            TIFFWriteTile(output_tiff, tile_pixels, x_coord, y_coord, 0, 0);

            ++tile_progress;
            int32_t tile_percent = (tile_progress * 100) / tiles_in_page;
            int32_t total_progress = ((*total_tiles_written + tile_progress) * 100) / total_tiles;

            // Calculate ETA
            clock_t current_global_time = clock();
            double elapsed_global_time = (double)(current_global_time - global_start_time) / CLOCKS_PER_SEC;
            double avg_time_per_tile = elapsed_global_time / (*total_tiles_written + tile_progress);
            double eta = avg_time_per_tile * (total_tiles - (*total_tiles_written + tile_progress));
            update_progress(total_progress, tile_percent, scale, eta);

            libisyntax_tile_free_pixels(pixels);
        }
    }

    // Write the directory for the current level.
    TIFFWriteDirectory(output_tiff);
}

int parse_cache_size(const char *size_str) {
    int size;
    char unit;

    if (sscanf(size_str, "%d%c", &size, &unit) == 2) {
        if (unit == 'M') {
            size *= 1024 * 1024;
        } else if (unit == 'G') {
            size *= 1024 * 1024 * 1024;
        } else {
            printf("Error: Invalid unit for cache size. Use 'M' for megabytes or 'G' for gigabytes.\n");
            return -1;
        }
    } else if (sscanf(size_str, "%d", &size) != 1) {
        printf("Error: Invalid cache size format.\n");
        return -1;
    }

    return size;
}


int main(int argc, char **argv) {
    char *filename = argv[1];
    char *output_tiffname = argv[2];

    int64_t cache_size = 2000;
    int32_t tile_size = 1024;

    int compression_type = COMPRESSION_JPEG;
    int quality = 80;

    for (int i = 3; i < argc; ++i) {
        if (strcmp(argv[i], "--tile-size") == 0) {
            if (i + 1 < argc) {
                tile_size = atoi(argv[i + 1]);
                if (tile_size <= 0) {
                    printf("Error: Invalid tile size. Please provide a positive integer value for the tile size.\n");
                    return -1;
                }
                i++; // Skip the next argument (tile size value)
            } else {
                printf("Error: Missing value for --tile-size option.\n");
                return -1;
            }
        } else if (strcmp(argv[i], "--compression") == 0) {
            if (i + 1 < argc) {
                if (strcmp(argv[i + 1], "JPEG") == 0) {
                    compression_type = COMPRESSION_JPEG;
                } else if (strcmp(argv[i + 1], "LZW") == 0) {
                    compression_type = COMPRESSION_LZW;
                } else if (strcmp(argv[i + 1], "NONE") == 0) {
                    compression_type = COMPRESSION_NONE;
                } else {
                    printf("Error: Invalid compression type. Supported types are JPEG, LZW, and NONE.\n");
                    return -1;
                }
                i++; // Skip the next argument (compression type value)
            } else {
                printf("Error: Missing value for --compression option.\n");
                return -1;
            }
        } else if (strcmp(argv[i], "--quality") == 0) {
            if (i + 1 < argc) {
                quality = atoi(argv[i + 1]);
                if (quality < 0 || quality > 100) {
                    printf("Error: Invalid quality value. Please provide an integer value between 0 and 100 for the quality.\n");
                    return -1;
                }
                if (compression_type != COMPRESSION_JPEG) {
                    printf("Warning: The --quality flag is ignored with the current compression type. Quality is only applicable to JPEG compressions.\n");
                }
                i++; // Skip the next argument (quality value)
            } else {
                printf("Error: Missing value for --quality option.\n");
                return -1;
            }
        } else if (strcmp(argv[i], "--cache-size") == 0) {
            if (i + 1 < argc) {
                cache_size = parse_cache_size(argv[i + 1]);
                if (cache_size < 0) {
                    return -1;
                }
                i++; // Skip the next argument (cache size value)
            } else {
                printf("Error: Missing value for --cache-size option.\n");
                return -1;
            }

        } else {
            printf("Error: Unknown option %s\n", argv[i]);
            return -1;
        }
    }
    int32_t tile_width = tile_size;
    int32_t tile_height = tile_size;

    libisyntax_init();

    isyntax_t *isyntax;
    if (libisyntax_open(filename, /*is_init_allocators=*/0, &isyntax) != LIBISYNTAX_OK) {
        printf("Failed to open %s\n", filename);
        return -1;
    }
    printf("Successfully opened %s\n", filename);

    int32_t internal_tile_width = libisyntax_get_tile_width(isyntax);
    int32_t internal_tile_height = libisyntax_get_tile_height(isyntax);
    LOG_VAR("%d", internal_tile_width);
    LOG_VAR("%d", internal_tile_height);

    isyntax_cache_t *isyntax_cache = NULL;
    assert(libisyntax_cache_create("isyntax-to-tiff cache", cache_size, &isyntax_cache) == LIBISYNTAX_OK);
    assert(libisyntax_cache_inject(isyntax_cache, isyntax) == LIBISYNTAX_OK);

    // Initialize the output TIFF file.
    TIFF *output_tiff;
    output_tiff = TIFFOpen(output_tiffname, "w8");
    if (!output_tiff) {
        printf("Failed to create %s\n", output_tiffname);
        return -1;
    }

    // Write all levels to the output TIFF.
    int start_at_page = 0;

    const isyntax_image_t *image = libisyntax_get_image(isyntax, 0);
    int32_t num_levels = libisyntax_image_get_level_count(image);
    int32_t total_tiles = 0;

    // Let's find the total number of tiles so we can have a progress counter
    for (int32_t level = start_at_page; level < num_levels; ++level) {
        isyntax_level_t *current_level = libisyntax_image_get_level(image, level);
        int32_t width = libisyntax_level_get_width(current_level);
        int32_t height = libisyntax_level_get_height(current_level);
        int32_t tiles_in_page = ((height - 1) / tile_height) * ((width - 1) / tile_width) + 2;
        total_tiles += tiles_in_page;
    }

    int32_t total_tiles_written = 0;
    clock_t global_start_time = clock();
    for (int32_t level = start_at_page; level < num_levels; ++level) {
        isyntax_level_t *current_level = libisyntax_image_get_level(image, level);
        write_page_to_tiff(output_tiff, isyntax, isyntax_cache, current_level, tile_width, tile_height,
                           &total_tiles_written, total_tiles, global_start_time, compression_type, quality);
    }

    // Close the output TIFF file.
    TIFFClose(output_tiff);

    // Clean up.
    libisyntax_cache_destroy(isyntax_cache);
    libisyntax_close(isyntax);

    return 0;
}
