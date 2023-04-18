#include "libisyntax.h"

#include <stdint.h>
#include <assert.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "third_party/stb_image_write.h"  // for png export

#define LOG_VAR(fmt, var) printf("%s: %s=" fmt "\n", __FUNCTION__, #var, var)

#include <stdint.h>
#if defined(__ARM_NEON)
#include <arm_neon.h>
#elif defined(__SSE2__)
#include <emmintrin.h>
#endif

void bgra_to_rgba(uint32_t* pixels, int tile_width, int tile_height) {
    int num_pixels = tile_width * tile_height;

#if defined(__ARM_NEON)
    for (int i = 0; i < num_pixels; i += 4) {
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
    for (int i = 0; i < num_pixels; i += 4) {
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
    for (int i = 0; i < num_pixels; ++i) {
        uint32_t val = pixels[i];
        pixels[i] = ((val & 0xff) << 16) | (val & 0x00ff00) | ((val & 0xff0000) >> 16) | (val & 0xff000000);
    }
#endif
}

void print_isyntax_levels(isyntax_t* isyntax) {
    int wsi_image_idx = libisyntax_get_wsi_image_index(isyntax);
    LOG_VAR("%d", wsi_image_idx);
    const isyntax_image_t* wsi_image = libisyntax_get_image(isyntax, wsi_image_idx);

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
               "       %s <isyntax_file> <level> <tile_x> <tile_y> <output.png> - write a tile to output.png",
               argv[0], argv[0]);
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

    if (argc <= 5) {
        print_isyntax_levels(isyntax);
    } else {
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
        assert(libisyntax_cache_create("example cache", 2000, &isyntax_cache) == LIBISYNTAX_OK);
        assert(libisyntax_cache_inject(isyntax_cache, isyntax) == LIBISYNTAX_OK);

        uint32_t *pixels = NULL;
        assert(libisyntax_tile_read(isyntax, isyntax_cache, level, tile_x, tile_y, &pixels) == LIBISYNTAX_OK);

        // convert data to the correct pixel format (bgra->rgba).
        bgra_to_rgba(pixels, tile_height, tile_width);

        printf("Writing %s...\n", output_png);
        stbi_write_png(output_png, tile_width, tile_height, 4, pixels, tile_width * 4);
        printf("Done writing %s.\n", output_png);

        libisyntax_tile_free_pixels(pixels);
        libisyntax_cache_destroy(isyntax_cache);
    }

    libisyntax_close(isyntax);
    return 0;
}