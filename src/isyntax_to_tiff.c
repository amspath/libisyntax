#include "libisyntax.h"

#include <stdint.h>
#include <assert.h>
#include <string.h>
#include <vips/vips.h>
#include <vips/foreign.h>
#include <glib-object.h>

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



typedef struct _VipsForeignLoadIsyntax {
    VipsForeignLoad parent_object;
    isyntax_t *isyntax;
    isyntax_cache_t *isyntax_cache;
    const isyntax_image_t *wsi_image;
    const isyntax_level_t *level;
    int32_t tile_width;
    int32_t tile_height;
    int32_t num_tiles_x;
    int32_t num_tiles_y;
} VipsForeignLoadIsyntax;

typedef VipsForeignLoadClass VipsForeignLoadIsyntaxClass;

G_DEFINE_TYPE(VipsForeignLoadIsyntax, vips_foreign_load_isyntax, VIPS_TYPE_FOREIGN_LOAD);


static int
isyntax_generate(VipsRegion *out, void *seq, void *a, void *b, gboolean *stop) {
    VipsForeignLoadIsyntax *isyntax = (VipsForeignLoadIsyntax *) a;
    VipsRect *r = &out->valid;

    int32_t tile_width = isyntax->tile_width;
    int32_t tile_height = isyntax->tile_height;

    int32_t level = 0;
    uint32_t *pixels = NULL;

    // Calculate the tile range for the region
    int32_t tile_start_x = r->left / tile_width;
    int32_t tile_end_x = (r->left + r->width + tile_width - 1) / tile_width;
    int32_t tile_start_y = r->top / tile_height;
    int32_t tile_end_y = (r->top + r->height + tile_height - 1) / tile_height;

    for (int32_t tile_y = tile_start_y; tile_y < tile_end_y; ++tile_y) {
        for (int32_t tile_x = tile_start_x; tile_x < tile_end_x; ++tile_x) {
            assert(libisyntax_tile_read(isyntax->isyntax, isyntax->isyntax_cache, level, tile_x, tile_y, &pixels) == LIBISYNTAX_OK);
            bgra_to_rgba(pixels, tile_width, tile_height);

            // Calculate the intersection of the region and the tile
            VipsRect tile_rect = {
                    .left = tile_x * tile_width,
                    .top = tile_y * tile_height,
                    .width = tile_width,
                    .height = tile_height
            };
            VipsRect intersection;
            vips_rect_intersectrect(r, &tile_rect, &intersection);

            // Copy the intersection area from the tile to the output region
            for (int y = intersection.top; y < VIPS_RECT_BOTTOM(&intersection); y++) {
                uint32_t *p = (uint32_t *) VIPS_REGION_ADDR(out, intersection.left, y);
                uint32_t *q = pixels + (y - tile_rect.top) * tile_width + (intersection.left - tile_rect.left);

                memcpy(p, q, intersection.width * sizeof(uint32_t));
            }
        }
    }

    // Free the pixels buffer if needed
    if (pixels) {
        free(pixels);
        /* Free the pixels buffer */
    }

    return (0);
}

/* Header function for VipsForeignLoadIsyntax */
static int
vips_foreign_load_isyntax_header(VipsForeignLoad *load) {
    VipsForeignLoadIsyntax *isyntax = (VipsForeignLoadIsyntax *)load;

    // Set the image properties using the appropriate libisyntax functions
    int width = /* Call libisyntax function to get width */;
    int height = /* Call libisyntax function to get height */;
    int bands = /* Call libisyntax function to get bands */;

    vips_image_init_fields(load->out,
                           width,
                           height,
                           bands,
                           VIPS_FORMAT_UCHAR, // Adjust this according to your image format
                           VIPS_CODING_NONE,
                           VIPS_INTERPRETATION_sRGB, // Adjust this according to your image interpretation
                           1.0, 1.0);

    return 0;
}


/* Class init function for VipsForeignLoadIsyntax */
static void
vips_foreign_load_isyntax_class_init(VipsForeignLoadIsyntaxClass *klass) {
    GObjectClass *gobject_class = G_OBJECT_CLASS(klass);
    VipsObjectClass *object_class = VIPS_OBJECT_CLASS(klass);
    VipsForeignLoadClass *foreign_load_class = (VipsForeignLoadClass *)klass;

    gobject_class->set_property = vips_object_set_property;
    gobject_class->get_property = vips_object_get_property;

    object_class->nickname = "isyntaxload";
    object_class->description = _("Load an isyntax image");

    /* Add your properties here using vips_object_class_install_property() */

    foreign_load_class->header = vips_foreign_load_isyntax_header;
    foreign_load_class->load = vips_foreign_load_isyntax_load;
}

/* Register the VipsForeignLoadIsyntax class with libvips */
static void
vips_foreign_load_isyntax_register(void) {
    static GOnce once = G_ONCE_INIT;

    if (g_once_init_enter(&once)) {
        GType type = g_type_register_static_simple(
                VIPS_TYPE_FOREIGN_LOAD,
                g_intern_static_string("VipsForeignLoadIsyntax"),
                sizeof(VipsForeignLoadIsyntaxClass),
                (GClassInitFunc)vips_foreign_load_isyntax_class_init,
                sizeof(VipsForeignLoadIsyntax),
                (GInstanceInitFunc)vips_foreign_load_isyntax_init,
                0);

        g_once_init_leave(&once, type);
    }
}

static void
vips_foreign_load_isyntax_init(VipsForeignLoadIsyntax *isyntax) {
    /* Initialize other members */

    isyntax->tile_width = libisyntax_get_tile_width(isyntax->isyntax);
    isyntax->tile_height = libisyntax_get_tile_height(isyntax->isyntax);
}



int main(int argc, char** argv) {
    if (VIPS_INIT(argv[0])) {
        vips_error_exit("Failed to initialize vips");
    }

    if (argc <= 1) {
        printf("Usage: %s <isyntax_file> <output.tiff> - convert an isyntax image to a tiff.", argv[0]);
        return 0;
    }

    char* filename = argv[1];
    char* output_file = argv[2];

    libisyntax_init();

    isyntax_t* isyntax;
    if (libisyntax_open(filename, /*is_init_allocators=*/0, &isyntax) != LIBISYNTAX_OK) {
        fprintf(stderr, "Failed to open %s\n", filename);
        return -1;
    }
    printf("Successfully opened %s\n", filename);

    isyntax_cache_t *isyntax_cache = NULL;
    assert(libisyntax_cache_create("tiff conversion cache", 200000, &isyntax_cache) == LIBISYNTAX_OK);
    assert(libisyntax_cache_inject(isyntax_cache, isyntax) == LIBISYNTAX_OK);

    int wsi_image_idx = libisyntax_get_wsi_image_index(isyntax);
    const isyntax_image_t* wsi_image = libisyntax_get_image(isyntax, wsi_image_idx);

    const isyntax_level_t *base_level = libisyntax_image_get_level(wsi_image, 0);
    const int32_t tile_height = libisyntax_get_tile_height(isyntax);
    const int32_t tile_width = libisyntax_get_tile_width(isyntax);
    const int32_t num_tiles_height = libisyntax_level_get_height_in_tiles(base_level);
    const int32_t num_tiles_width = libisyntax_level_get_width_in_tiles(base_level);
    // TODO: The actual width is smaller! Get this from the library.

    uint32_t *pixels = NULL;

    int32_t total_tiles = num_tiles_width * num_tiles_height;
    int32_t processed_tiles = 0;
    int prev_progress = -1; // Initialize to an invalid value

    for (int32_t tile_y = 0; tile_y < num_tiles_height; ++tile_y) {
        for (int32_t tile_x = 0; tile_x < num_tiles_width; ++tile_x) {
            assert(libisyntax_tile_read(isyntax, isyntax_cache, 0, tile_x, tile_y, &pixels) == LIBISYNTAX_OK);
            bgra_to_rgba(pixels, tile_height, tile_width);
            // Do something with the tile.

            // Update the progress counter
            processed_tiles++;

            // Calculate the current progress as a percentage
            int current_progress = (int)((float)processed_tiles / total_tiles * 100.0);

            // Print the progress to the console only when it changes
            if (current_progress != prev_progress) {
                printf("Progress: %d%%\r", current_progress);
                fflush(stdout); // Flush the output buffer to ensure it gets printed immediately

                // Update the previous progress value
                prev_progress = current_progress;
            }
        }
    }




    libisyntax_tile_free_pixels(pixels);
    libisyntax_cache_destroy(isyntax_cache);
    libisyntax_close(isyntax);
    vips_shutdown();

    return 0;
}
