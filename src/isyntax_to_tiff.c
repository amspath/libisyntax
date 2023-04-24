#include "libisyntax.h"
#include "tiffio.h"
#include <stdint.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>


// https://stackoverflow.com/questions/11228855/header-files-for-x86-simd-intrinsics
#if defined(_MSC_VER)
/* Microsoft C/C++-compatible compiler */
     #include <intrin.h>
#elif defined(__GNUC__) && (defined(__x86_64__) || defined(__i386__))
/* GCC-compatible compiler, targeting x86/x86-64 */
#include <x86intrin.h>
#elif defined(__GNUC__) && defined(__ARM_NEON__)
/* GCC-compatible compiler, targeting ARM with NEON */
#include <arm_neon.h>
#elif defined(__GNUC__) && defined(__IWMMXT__)
/* GCC-compatible compiler, targeting ARM with WMMX */
     #include <mmintrin.h>
#elif (defined(__GNUC__) || defined(__xlC__)) && (defined(__VEC__) || defined(__ALTIVEC__))
     /* XLC or GCC-compatible compiler, targeting PowerPC with VMX/VSX */
     #include <altivec.h>
#elif defined(__GNUC__) && defined(__SPE__)
     /* GCC-compatible compiler, targeting PowerPC with SPE */
     #include <spe.h>
#endif

void update_progress(int32_t total_progress, int32_t page_progress, int32_t page_number, double eta) {
    printf("\rProgress: %3d%% | Page %d progress: %3d%% | ETA: %.0fs", total_progress, page_number, page_progress, eta);
    fflush(stdout);
}

void bgra_to_rgba(uint32_t *pixels, int tile_width, int tile_height) {
    int num_pixels = tile_width * tile_height;
    int num_pixels_aligned = (num_pixels / 4) * 4;

#if defined(__ARM_NEON)
    for (int i = 0; i < num_pixels_aligned; i += 4) {
        uint32x4_t bgra = vld1q_u32(pixels + i);
        uint32x4_t b_mask = vdupq_n_u32(0x000000FF);
        uint32x4_t r_mask = vdupq_n_u32(0x00FF0000);
        uint32x4_t b = vandq_u32(bgra, b_mask);
        uint32x4_t r = vandq_u32(bgra, r_mask);
        uint32x4_t br_swapped = vorrq_u32(vshlq_n_u32(b, 16), vshrq_n_u32(r, 16));
        uint32x4_t ga_alpha_mask = vdupq_n_u32(0xFF00FF00);
        uint32x4_t ga_alpha = vandq_u32(bgra, ga_alpha_mask);
        uint32x4_t rgba = vorrq_u32(ga_alpha, br_swapped);
        vst1q_u32(pixels + i, rgba);
    }
#elif defined(__SSE2__)
    for (int i = 0; i < num_pixels_aligned; i += 4) {
        __m128i bgra = _mm_loadu_si128((__m128i*)(pixels + i));
        __m128i b_mask = _mm_set1_epi32(0x000000FF);
        __m128i r_mask = _mm_set1_epi32(0x00FF0000);
        __m128i b = _mm_and_si128(bgra, b_mask);
        __m128i r = _mm_and_si128(bgra, r_mask);
        __m128i br_swapped = _mm_or_si128(_mm_slli_epi32(b, 16), _mm_srli_epi32(r, 16));
        __m128i ga_alpha_mask = _mm_set1_epi32(0xFF00FF00);
        __m128i ga_alpha = _mm_and_si128(bgra, ga_alpha_mask);
        __m128i rgba = _mm_or_si128(ga_alpha, br_swapped);
        _mm_storeu_si128((__m128i*)(pixels + i), rgba);
    }
#else
    for (int i = num_pixels_aligned; i < num_pixels; ++i) {
        uint32_t val = pixels[i];
        pixels[i] = ((val & 0xff) << 16) | (val & 0x00ff00) | ((val & 0xff0000) >> 16) | (val & 0xff000000);
    }
#endif
}

void write_page_to_tiff(TIFF *output_tiff, isyntax_t *isyntax, isyntax_cache_t *isyntax_cache, isyntax_level_t *level,
                        int32_t tile_width, int32_t tile_height, const int32_t *total_tiles_written, int32_t total_tiles,
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

    if (scale == 0) {
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

            // In case our actual tile is smaller, we need to convert it to a full tile.
            if (region_width != tile_width || region_height != tile_height) {
                tile_pixels = calloc(tile_width * tile_height, sizeof(uint32_t));
                for (int32_t row = 0; row < region_height; ++row) {
                    memcpy(tile_pixels + row * tile_width, pixels + row * region_width,
                           region_width * sizeof(uint32_t));
                }
            }

            // Write the tile to the output TIFF.
            TIFFWriteTile(output_tiff, tile_pixels, x_coord, y_coord, 0, 0);
            if (tile_pixels != pixels) {
                free(tile_pixels);
            }

            ++tile_progress;
            int32_t tile_percent = (tile_progress * 100) / tiles_in_page;
            int32_t total_progress = ((*total_tiles_written + tile_progress) * 100) / total_tiles;

            // Calculate ETA
            clock_t current_global_time = clock();
            double elapsed_global_time = (double) (current_global_time - global_start_time) / CLOCKS_PER_SEC;
            double avg_time_per_tile = elapsed_global_time / (*total_tiles_written + tile_progress);
            double eta = avg_time_per_tile * (total_tiles - (*total_tiles_written + tile_progress));
            update_progress(total_progress, tile_percent, scale, eta);

            libisyntax_tile_free_pixels(pixels);
        }
    }

    // Write the directory for the current level.
    TIFFWriteDirectory(output_tiff);
}

uint64_t parse_cache_size(const char *size_str) {
    uint64_t size;
    char unit;

    if (sscanf(size_str, "%lld%c", &size, &unit) == 2) {
        if (unit == 'M') {
            if (size > INT64_MAX / (1024)) {
                printf("Error: Cache size too large.\n");
                return -1;
            }
            size *= 1024;
        } else if (unit == 'G') {
            if (size > INT64_MAX / (1024 * 1024)) {
                printf("Error: Cache size too large.\n");
                return -1;
            }
            size *= 1024 * 1024;
        } else {
            printf("Error: Invalid unit for cache size. Use 'M' for megabytes or 'G' for gigabytes.\n");
            return -1;
        }
    } else if (sscanf(size_str, "%lld", &size) != 1) {
        printf("Error: Invalid cache size format.\n");
        return -1;
    }

    return size;
}


int main(int argc, char **argv) {
    const char *usage_string =
            "Usage: isyntax-to-tiff [OPTIONS] INPUT OUTPUT\n\n"
            "Converts Philips iSyntax files to a multi-resolution BigTIFF file.\n\n"
            "Positional arguments:\n"
            "  INPUT                 Path to the input iSyntax file.\n"
            "  OUTPUT                Path to the output TIFF file.\n\n"
            "Options:\n"
            "  --help                Show this help message and exit.\n\n"
            "  --start-at-page PAGE  Specifies the page to start at (default: 0).\n"
            "  --tile-size SIZE      Specifies the tile size for the output TIFF (default: 1024).\n"
            "                        Must be a positive integer.\n\n"
            "  --compression TYPE    Specifies the compression type for the output TIFF.\n"
            "                        Supported types: JPEG, LZW, NONE (default: JPEG).\n\n"
            "  --quality VALUE       Specifies the quality for JPEG compression (0-100).\n"
            "                        Only applicable when using JPEG compression (default: 80).\n\n"
            "  --cache-size SIZE     Specifies the cache size for the iSyntax library.\n"
            "                        Accepts a number followed by 'M' (for megabytes) or 'G' (for gigabytes),\n"
            "                        or just a number for kilobytes (default: 2000).\n\n"
            "Example:\n\n"
            "  isyntax-to-tiff --tile-size 512 --compression JPEG --quality 90 --cache-size 1G input.isyntax output.tiff\n\n"
            "This command will convert the input.isyntax file into an output.tiff file with a tile size of 512, JPEG compression at 90 quality, and a cache size of 1 gigabyte.\n";

    if (argc < 3) {
        printf("Error: Missing input and/or output file arguments.\n\n");
        printf("%s", usage_string);
        return -1;
    }

    char *filename = argv[1];
    char *output_tiffname = argv[2];

    uint64_t cache_size = 2000;
    int32_t tile_size = 1024;
    int start_at_page = 0;

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
        } else if (strcmp(argv[i], "--help") == 0) {
            printf("%s", usage_string);
            return 0;
        } else if (strcmp(argv[i], "--start-at-page") == 0) {
            if (i + 1 < argc) {
                start_at_page = atoi(argv[i + 1]);
                if (start_at_page < 0) {
                    printf("Error: Invalid page number. Please provide a positive integer value for the page number.\n");
                    return -1;
                }
                i++; // Skip the next argument (page number value)
            } else {
                printf("Error: Missing value for --start-at-page option.\n");
                return -1;
            }
        } else if (strcmp(argv[i], "--cache-size") == 0) {
            if (i + 1 < argc) {
                cache_size = parse_cache_size(argv[i + 1]);
                if (cache_size >= INT64_MAX || cache_size < 0) {
                    printf("Error: Cache size not suitable for the system.\n");
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
        fprintf(stderr, "Failed to open %s\n", filename);
        return -1;
    }


    isyntax_cache_t *isyntax_cache = NULL;
    if (libisyntax_cache_create("isyntax-to-tiff cache", cache_size, &isyntax_cache) != LIBISYNTAX_OK) {
        fprintf(stderr, "Failed to create iSyntax cache with size %llu.\n", cache_size);
        libisyntax_close(isyntax);
        return -1;
    }
    if (libisyntax_cache_inject(isyntax_cache, isyntax) != LIBISYNTAX_OK) {
        fprintf(stderr, "Failed to inject iSyntax cache into iSyntax instance.\n");
        libisyntax_cache_destroy(isyntax_cache);
        libisyntax_close(isyntax);
        return -1;
    }

    // Initialize the output TIFF file.
    TIFF *output_tiff;
    output_tiff = TIFFOpen(output_tiffname, "w8");
    if (!output_tiff) {
        fprintf(stderr, "Failed to create %s\n", output_tiffname);
        return -1;
    }

    const isyntax_image_t *image = libisyntax_get_image(isyntax, 0);

    // Check if the page we selected actually exists.
    int32_t level_count = libisyntax_image_get_level_count(image);
    if (start_at_page >= level_count) {
        fprintf(stderr, "Error: The page number %d is out of range. The image only has %d pages. Set --start-at-page to a smaller value.\n", start_at_page, level_count);
        libisyntax_cache_destroy(isyntax_cache);
        libisyntax_close(isyntax);
        TIFFClose(output_tiff);
        return -1;
    }

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
