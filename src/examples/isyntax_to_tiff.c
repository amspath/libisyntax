/*
  BSD 2-Clause License

  Copyright (c) 2019-2024, Pieter Valkema, Alexandr Virodov, Jonas Teuwen

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

#include "libisyntax.h"

#ifdef LINK_LIBTIFF_AT_RUNTIME
// On Windows, CMake will #define LINK_LIBTIFF_AT_RUNTIME if find_package(TIFF) fails.
// In this case, we will load the libtiff procedures from the DLL at runtime.
#define LIBTIFF_API_IMPL
#include "utils/libtiff_api.h"
#else
#include "tiffio.h"
#endif

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

#define CHECK_LIBISYNTAX_OK(_libisyntax_call) do { \
    isyntax_error_t result = _libisyntax_call;     \
    assert(result == LIBISYNTAX_OK);               \
} while(0);

void update_progress(int32_t total_progress, int32_t page_progress, int32_t page_number, double eta) {
    int extra_spaces = 2;
    printf("\rProgress: %3d%% | Page %d progress: %3d%% | ETA: %.0fs%*s", total_progress, page_number, page_progress, eta, extra_spaces, "");
    printf("\033[%dD", extra_spaces);
    fflush(stdout);
}

void rgba_to_rgb(uint32_t *pixels, int width, int height, uint8_t* out_pixels) {
	int num_pixels = width * height;
	uint8_t* src_pos = (uint8_t*)pixels;
	uint8_t* dst_pos = out_pixels;
	for (int i = 0; i < num_pixels; ++i) {
		dst_pos[0] = src_pos[0];
		dst_pos[1] = src_pos[1];
		dst_pos[2] = src_pos[2];
		dst_pos += 3;
		src_pos += 4;
	}
}

void write_page_to_tiff(TIFF *output_tiff, isyntax_t *isyntax, isyntax_cache_t *isyntax_cache, const isyntax_level_t *level,
                        int32_t tile_width, int32_t tile_height, int32_t *total_tiles_written, int32_t total_tiles,
                        clock_t global_start_time, uint16_t compression_type, uint16_t quality,
						uint32_t photometric_interpretation, int32_t samples_per_pixel) {
    int32_t width = libisyntax_level_get_width(level);
    int32_t height = libisyntax_level_get_height(level);
    int32_t scale = libisyntax_level_get_scale(level);

    // Set the TIFF properties for the current level.
    TIFFSetField(output_tiff, TIFFTAG_IMAGEWIDTH, width);
    TIFFSetField(output_tiff, TIFFTAG_IMAGELENGTH, height);
    TIFFSetField(output_tiff, TIFFTAG_BITSPERSAMPLE, 8);
    TIFFSetField(output_tiff, TIFFTAG_SAMPLESPERPIXEL, samples_per_pixel);

    if (compression_type == COMPRESSION_JPEG) {
        TIFFSetField(output_tiff, TIFFTAG_COMPRESSION, COMPRESSION_JPEG);
        TIFFSetField(output_tiff, TIFFTAG_JPEGQUALITY, quality);
    } else if (compression_type == COMPRESSION_LZW) {
        TIFFSetField(output_tiff, TIFFTAG_COMPRESSION, COMPRESSION_LZW);
    }

	if (compression_type == COMPRESSION_JPEG && photometric_interpretation == PHOTOMETRIC_YCBCR) {
		TIFFSetField(output_tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_YCBCR);
		TIFFSetField(output_tiff, TIFFTAG_JPEGCOLORMODE, JPEGCOLORMODE_RGB); // Pseudo-tag: convert to/from RGB
	} else {
		TIFFSetField(output_tiff, TIFFTAG_PHOTOMETRIC, PHOTOMETRIC_RGB);
	}

	assert(samples_per_pixel == 3 || samples_per_pixel == 4);
	if (samples_per_pixel == 4) {
		// Extra sample is interpreted as alpha channel
		TIFFSetField(output_tiff, TIFFTAG_EXTRASAMPLES, 1, (uint16_t[]) {EXTRASAMPLE_ASSOCALPHA});
	}

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

    if (scale == 0) {
        TIFFSetField(output_tiff, TIFFTAG_SUBFILETYPE, 0);
    } else {
        TIFFSetField(output_tiff, TIFFTAG_SUBFILETYPE, FILETYPE_REDUCEDIMAGE);
    }

    int32_t tile_progress = 0;
    int32_t tiles_in_page = ((height + tile_height - 1) / tile_height) * ((width + tile_width - 1) / tile_width);

    // Allocate enough memory to hold a region up to the size of a full tile, and re-use that
    // TODO(pvalkema): refactor
    uint32_t* region_pixels = (uint32_t*)malloc(tile_width * tile_height * sizeof(uint32_t));

    // Allocate enough memory to be able to convert a smaller region to a full tile
    uint32_t* full_tile_pixels = (uint32_t*)malloc(tile_width * tile_height * sizeof(uint32_t));

	uint8_t* tile_pixels_rgb = NULL;
	if (samples_per_pixel == 3) {
		// Allocate memory for RGB pixels (without alpha channel)
		tile_pixels_rgb = (uint8_t*)malloc(tile_width * tile_height * 3);
	}

    for (int32_t y_coord = 0; y_coord < height; y_coord += tile_height) {
        for (int32_t x_coord = 0; x_coord < width; x_coord += tile_width) {
            // At the borders the tile size can be smaller
            int32_t region_width = (x_coord + tile_width > width) ? width - x_coord : tile_width;
            int32_t region_height = (y_coord + tile_height > height) ? height - y_coord : tile_height;


            // Checking if we are reading within bounds
            assert(x_coord + region_width <= width);
            assert(y_coord + region_height <= height);

            // TODO(pvalkema): make libisyntax_read_region() robust for out-of-bounds reading,
            //  adding white pixels in the out-of-bounds area
            CHECK_LIBISYNTAX_OK(libisyntax_read_region(isyntax, isyntax_cache, scale, x_coord, y_coord, region_width, region_height,
                                          region_pixels, LIBISYNTAX_PIXEL_FORMAT_RGBA));

            // In case our actual tile is smaller, we need to convert it to a full tile.
            uint32_t* final_tile_pixels_rgba = NULL;
            if (region_width != tile_width || region_height != tile_height) {
                memset(full_tile_pixels, 0xFF, tile_width * tile_height * sizeof(uint32_t));
                for (int32_t row = 0; row < region_height; ++row) {
                    memcpy(full_tile_pixels + row * tile_width, region_pixels + row * region_width,
                           region_width * sizeof(uint32_t));
                }
	            final_tile_pixels_rgba = full_tile_pixels;
            } else {
	            final_tile_pixels_rgba = region_pixels;
            }

			uint8_t* final_tile_pixels = (uint8_t*)final_tile_pixels_rgba;
	        if (samples_per_pixel == 3) {
		        // Convert RGBA to RGB (discard alpha channel).
		        rgba_to_rgb(final_tile_pixels_rgba, tile_width, tile_height, tile_pixels_rgb);
				final_tile_pixels = tile_pixels_rgb;
			}

            // Write the tile to the output TIFF.
            TIFFWriteTile(output_tiff, final_tile_pixels, x_coord, y_coord, 0, 0);

            ++tile_progress;
            *total_tiles_written += 1;
            int32_t tile_percent = (tile_progress * 100) / tiles_in_page;
            int32_t total_progress = ((*total_tiles_written) * 100) / total_tiles;

            clock_t current_global_time = clock();
            double elapsed_global_time = (double) (current_global_time - global_start_time) / CLOCKS_PER_SEC;
            double avg_time_per_tile = (*total_tiles_written > 0) ? (elapsed_global_time / *total_tiles_written) : 0;
            double eta = avg_time_per_tile * (total_tiles - *total_tiles_written);
            update_progress(total_progress, tile_percent, scale, eta);
        }
    }

    free(full_tile_pixels);
    free(region_pixels);
	if (tile_pixels_rgb) free(tile_pixels_rgb);

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
            "Usage: isyntax-to-tiff INPUT OUTPUT [OPTIONS]\n\n"
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
            "  --color-space TYPE    Specifies the color space for the output TIFF.\n"
			"                        Only applicable when using JPEG compression.\n"
            "                        Supported types: YCbCr, RGB (default: YCbCr).\n\n"
			"  --add-alpha 0|1       Specifies whether to add an alpha channel (default: 0).\n\n"
            "  --cache-size SIZE     Specifies the cache size for the iSyntax library.\n"
            "                        Accepts a number followed by 'M' (for megabytes) or 'G' (for gigabytes),\n"
            "                        or just a number for kilobytes (default: 2000).\n\n"
            "Example:\n\n"
            "  isyntax-to-tiff input.isyntax output.tiff --tile-size 512 --compression JPEG --quality 90 --cache-size 1G \n\n"
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
	uint32_t photometric_interpretation = PHOTOMETRIC_YCBCR;
	int samples_per_pixel = 3;

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

        } else if (strcmp(argv[i], "--color-space") == 0) {
	        if (i + 1 < argc) {
		        if (strcasecmp(argv[i + 1], "YCbCr") == 0) {
			        photometric_interpretation = PHOTOMETRIC_YCBCR;
		        } else if (strcmp(argv[i + 1], "RGB") == 0) {
			        photometric_interpretation = PHOTOMETRIC_RGB;
		        } else {
			        printf("Error: Invalid color space. Supported types are YCbCr and RGB.\n");
			        return -1;
		        }
		        i++; // Skip the next argument (color space value)
	        } else {
		        printf("Error: Missing value for --color-space option.\n");
		        return -1;
	        }
        } else if (strcmp(argv[i], "--add-alpha") == 0) {
	        if (i + 1 < argc) {
		        if (strcasecmp(argv[i + 1], "0") == 0) {
			        samples_per_pixel = 3;
		        } else if (strcmp(argv[i + 1], "1") == 0) {
			        samples_per_pixel = 4;
		        } else {
			        printf("Error: Invalid value for --add-alpha option. Please provide 0 or 1.\n");
			        return -1;
		        }
		        i++; // Skip the next argument (add alpha type value)
	        } else {
		        printf("Error: Missing value for --add-alpha option.\n");
		        return -1;
	        }
        } else {
            printf("Error: Unknown option %s\n", argv[i]);
            return -1;
        }
    }

	if (samples_per_pixel == 4 && compression_type == COMPRESSION_JPEG && photometric_interpretation == PHOTOMETRIC_YCBCR) {
		// This combination unfortunately will cause errors during encoding: "JPEGLib: Bogus input colorspace."
		printf("Warning: The --add-alpha option does not work when using JPEG compression with the YCbCr color space.\n"
			   "To add an alpha channel, either use the RGB color space or pick another compression type.\n"
			   "Alpha channel will be disabled.\n");
		samples_per_pixel = 3;
	}

    int32_t tile_width = tile_size;
    int32_t tile_height = tile_size;

    libisyntax_init();
#ifdef LINK_LIBTIFF_AT_RUNTIME
	init_libtiff_at_runtime();
#endif

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

    const isyntax_image_t *image = libisyntax_get_wsi_image(isyntax);

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
        const isyntax_level_t *current_level = libisyntax_image_get_level(image, level);
        int32_t width = libisyntax_level_get_width(current_level);
        int32_t height = libisyntax_level_get_height(current_level);
        int32_t tiles_in_page = ((height + tile_height - 1) / tile_height) * ((width + tile_width - 1) / tile_width);

        total_tiles += tiles_in_page;
    }

    int32_t total_tiles_written = 0;
    clock_t global_start_time = clock();
    for (int32_t level = start_at_page; level < num_levels; ++level) {
        const isyntax_level_t *current_level = libisyntax_image_get_level(image, level);
        write_page_to_tiff(output_tiff, isyntax, isyntax_cache, current_level, tile_width, tile_height,
                           &total_tiles_written, total_tiles, global_start_time, compression_type, quality,
						   photometric_interpretation, samples_per_pixel);
    }

    // Close the output TIFF file.
    TIFFClose(output_tiff);

    // Clean up.
    libisyntax_cache_destroy(isyntax_cache);
    libisyntax_close(isyntax);

    return 0;
}
