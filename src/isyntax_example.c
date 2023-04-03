#include "common.h"

#include "isyntax.h"

#define LOG_VAR(fmt, var) printf("%s: %s=" fmt "\n", __FUNCTION__, #var, var)

int main(int argc, char** argv) {

	if (argc <= 1) {
        printf("Usage: %s <isyntax_file> - show levels & tiles.\n"
               "       %s <isyntax_file> <scale> <tile_x> <tile_y> <output.png> - write a tile to output.png",
               argv[0], argv[0]);
		return 0;
	}

	char* filename = argv[1];

	isyntax_t isyntax = {0};
	if (isyntax_open(&isyntax, filename)) {
		printf("Successfully opened %s\n", filename);

        int wsi_image_idx = isyntax.wsi_image_index;
        LOG_VAR("%d", wsi_image_idx);
        isyntax_image_t* wsi_image = &isyntax.images[wsi_image_idx];
        isyntax_level_t* levels = wsi_image->levels;

        for (int i = 0; i < wsi_image->level_count; ++i) {
            LOG_VAR("%d", i);
            LOG_VAR("%d", levels[i].scale);
            LOG_VAR("%d", levels[i].width_in_tiles);
            LOG_VAR("%d", levels[i].height_in_tiles);
        }

        isyntax_destroy(&isyntax);
	}

	return 0;
}
