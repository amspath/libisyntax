#include "libisyntax.h"

#include <stdint.h>
#include <assert.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION

#include "third_party/stb_image_write.h"  // for png export

#if defined(__ARM_NEON)

#include <arm_neon.h>

#elif defined(__SSE2__)
#include <emmintrin.h>
#endif


#define LOG_VAR(fmt, var) printf("%s: %s=" fmt "\n", __FUNCTION__, #var, var)

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
