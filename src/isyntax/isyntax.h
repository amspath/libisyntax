/*
  BSD 2-Clause License

  Copyright (c) 2019-2023, Pieter Valkema

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

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "common.h"
//#include "platform.h"
#include "block_allocator.h"
#include "work_queue.h"

#include "yxml.h"

#define DWT_COEFF_BITS 16
#if (DWT_COEFF_BITS==16)
typedef i16 icoeff_t;
#else
typedef i32 icoeff_t;
#endif

#define ISYNTAX_IDWT_PAD_L 4
#define ISYNTAX_IDWT_PAD_R 4
#define ISYNTAX_IDWT_FIRST_VALID_PIXEL 7

#define ISYNTAX_ADJ_TILE_TOP_LEFT 0x100
#define ISYNTAX_ADJ_TILE_TOP_CENTER 0x80
#define ISYNTAX_ADJ_TILE_TOP_RIGHT 0x40
#define ISYNTAX_ADJ_TILE_CENTER_LEFT 0x20
#define ISYNTAX_ADJ_TILE_CENTER 0x10
#define ISYNTAX_ADJ_TILE_CENTER_RIGHT 8
#define ISYNTAX_ADJ_TILE_BOTTOM_LEFT 4
#define ISYNTAX_ADJ_TILE_BOTTOM_CENTER 2
#define ISYNTAX_ADJ_TILE_BOTTOM_RIGHT 1


enum isyntax_image_type_enum {
	ISYNTAX_IMAGE_TYPE_NONE = 0,
	ISYNTAX_IMAGE_TYPE_MACROIMAGE = 1,
	ISYNTAX_IMAGE_TYPE_LABELIMAGE = 2,
	ISYNTAX_IMAGE_TYPE_WSI = 3,
};

enum isyntax_node_type_enum {
	ISYNTAX_NODE_NONE = 0,
	ISYNTAX_NODE_LEAF = 1, // ex. <Attribute Name="DICOM_MANUFACTURER" Group="0x0008" Element="0x0070"PMSVR="IString">PHILIPS</ Attribute>
	ISYNTAX_NODE_BRANCH = 2, // ex. <DataObject ObjectType="DPScannedImage"> (leaf nodes) </DataObject>
	ISYNTAX_NODE_ARRAY = 3, // <Array> (contains one or more similar type of leaf/branch nodes)
};

// NOTE: Most of these have DICOM group 0x301D. Currently there seem to be no element ID collisions.
enum isyntax_group_data_object_dicom_element_enum {
	// Group 0x301D
	PIM_DP_SCANNED_IMAGES                   = 0x1003, // DPScannedImage
	DP_IMAGE_POST_PROCESSING                = 0x1014, // DPImagePostProcessing
	DP_WAVELET_QUANTIZER_SETTINGS_PER_COLOR = 0x1019, // DPWaveletQuantizerSeetingsPerColor
	DP_WAVELET_QUANTIZER_SETTINGS_PER_LEVEL = 0x101a, // DPWaveletQuantizerSeetingsPerLevel
	UFS_IMAGE_GENERAL_HEADERS               = 0x2000, // UFSImageGeneralHeader
	UFS_IMAGE_DIMENSIONS                    = 0x2003, // UFSImageDimension
	UFS_IMAGE_BLOCK_HEADER_TEMPLATES        = 0x2009, // UFSImageBlockHeaderTemplate
	UFS_IMAGE_DIMENSION_RANGES              = 0x200a, // UFSImageDimensionRange
	DP_COLOR_MANAGEMENT                     = 0x200b, // DPColorManagement
	UFS_IMAGE_BLOCK_HEADERS                 = 0x200d, // UFSImageBlockHeader              // new in iSyntax v2
	UFS_IMAGE_CLUSTER_HEADER_TEMPLATES      = 0x2016, // UFSImageClusterHeaderTemplate    // new in iSyntax v2
	UFS_IMAGE_VALID_DATA_ENVELOPES          = 0x2023, // UFSImageValidDataEnvelope        // new in iSyntax v2
	UFS_IMAGE_OPP_EXTREME_VERTICES          = 0x2024, // UFSImageOppExtremeVertex         // new in iSyntax v2
	// Group 8B01
	PIIM_PIXEL_DATA_REPRESENTATION_SEQUENCE = 0x1001, // PixelDataRepresentation
};

enum isyntax_data_object_flag_enum {
	ISYNTAX_OBJECT_DPUfsImport = 1,
	ISYNTAX_OBJECT_DPScannedImage = 2,
	ISYNTAX_OBJECT_UFSImageGeneralHeader = 4,
	ISYNTAX_OBJECT_UFSImageBlockHeaderTemplate = 8,
	ISYNTAX_OBJECT_UFSImageDimension = 0x10,
	ISYNTAX_OBJECT_UFSImageDimensionRange = 0x20,
	ISYNTAX_OBJECT_DPColorManagement = 0x40,
	ISYNTAX_OBJECT_DPImagePostProcessing = 0x80,
	ISYNTAX_OBJECT_DPWaveletQuantizerSeetingsPerColor = 0x100,
	ISYNTAX_OBJECT_DPWaveletQuantizerSeetingsPerLevel = 0x200,
	ISYNTAX_OBJECT_PixelDataRepresentation = 0x400,
	ISYNTAX_OBJECT_UFSImageBlockHeader = 0x800,             // new in iSyntax v2
	ISYNTAX_OBJECT_UFSImageClusterHeaderTemplate = 0x1000,  // new in iSyntax v2
	ISYNTAX_OBJECT_UFSImageValidDataEnvelope = 0x2000,      // new in iSyntax v2
	ISYNTAX_OBJECT_UFSImageOppExtremeVertex = 0x4000,       // new in iSyntax v2
};


#pragma pack(push, 1)
typedef struct isyntax_dicom_tag_header_t {
	u16 group;
	u16 element;
	u32 size;
} isyntax_dicom_tag_header_t;

typedef struct isyntax_partial_block_header_t {
	isyntax_dicom_tag_header_t sequence_element_header;
	isyntax_dicom_tag_header_t block_coordinates_header;
	u32 x_coordinate;
	u32 y_coordinate;
	u32 color_component;
	u32 scale;
	u32 coefficient;
	/* [MISSING] dicom_tag_header_t block_data_offset_header; */
	/* [MISSING] u64 block_data_offset; */
	/* [MISSING] dicom_tag_header_t block_size_header; */
	/* [MISSING] u64 block_size; */
	isyntax_dicom_tag_header_t block_header_template_id_header;
	u32 block_header_template_id;
} isyntax_partial_block_header_t;

typedef struct isyntax_full_block_header_t {
	isyntax_dicom_tag_header_t sequence_element_header;
	isyntax_dicom_tag_header_t block_coordinates_header;
	u32 x_coordinate;
	u32 y_coordinate;
	u32 color_component;
	u32 scale;
	u32 coefficient;
	isyntax_dicom_tag_header_t block_data_offset_header;
	u64 block_data_offset;
	isyntax_dicom_tag_header_t block_size_header;
	u64 block_size;
	isyntax_dicom_tag_header_t block_header_template_id_header;
	u32 block_header_template_id;
} isyntax_full_block_header_t;

typedef struct isyntax_seektable_codeblock_header_t {
	isyntax_dicom_tag_header_t start_header;
	isyntax_dicom_tag_header_t block_data_offset_header;
	u64 block_data_offset;
	isyntax_dicom_tag_header_t block_size_header;
	u64 block_size;
} isyntax_seektable_codeblock_header_t;
#pragma pack(pop)

typedef struct isyntax_image_dimension_range_t {
	i32 start;
	i32 step;
	i32 end;
	i32 numsteps;
} isyntax_image_dimension_range_t;

typedef struct isyntax_block_header_template_t {
	u32 block_width;     // e.g. 128
	u32 block_height;    // e.g. 128
	u8 color_component;  // 0=Y 1=Co 2=Cg
	u8 scale;            // range 0-8
	u8 waveletcoeff;     // either 1 for LL, or 3 for LH+HL+HH
} isyntax_block_header_template_t;

typedef struct isyntax_cluster_block_header_t {
	u32 x_coordinate;
	u32 y_coordinate;
	u32 color_component;
	u32 scale;
	u32 coefficient;
} isyntax_cluster_block_header_t;

typedef struct isyntax_cluster_relative_coords_t {
	u32 raw_coords[5];
	u32 block_header_template_id;
	u32 x;
	u32 y;
	u32 color_component;
	u32 scale;
	u32 waveletcoeff;
} isyntax_cluster_relative_coords_t;

#define MAX_CODEBLOCKS_PER_CLUSTER 70 // NOTE: what is the actual maximum possible?

typedef struct isyntax_cluster_header_template_t {
	u32 base_x;
	u32 base_y;
	u8 base_scale;
	u8 base_waveletcoeff;
	u8 base_color_component;
	isyntax_cluster_relative_coords_t relative_coords_for_codeblock_in_cluster[MAX_CODEBLOCKS_PER_CLUSTER];
	i32 codeblock_in_cluster_count;
	i32 dimension_order[5];
	u8 dimension_count;
} isyntax_cluster_header_template_t;

typedef struct isyntax_codeblock_t {
	u32 x_coordinate;
	u32 y_coordinate;
	u32 color_component;
	u32 scale;
	u32 coefficient;
	u64 block_data_offset;
	u64 block_size;
	u32 block_header_template_id;
	i32 x_adjusted;
	i32 y_adjusted;
	i32 block_x;
	i32 block_y;
	u64 block_id;
} isyntax_codeblock_t;

typedef struct isyntax_data_chunk_t {
	i64 offset;
	u32 size;
	i32 top_codeblock_index;
	i32 codeblock_count_per_color;
	i32 scale;
	i32 level_count;
	u8* data;
} isyntax_data_chunk_t;

typedef struct isyntax_tile_channel_t {
	icoeff_t* coeff_h;
	icoeff_t* coeff_ll;
	u32 neighbors_loaded;
} isyntax_tile_channel_t;

typedef struct isyntax_tile_t {
	u32 codeblock_index;
	u32 codeblock_chunk_index;
	u32 data_chunk_index;
	isyntax_tile_channel_t color_channels[3];
	u32 ll_invalid_edges;
	bool exists;
	bool has_ll;
	bool has_h;
	bool is_submitted_for_h_coeff_decompression;
	bool is_submitted_for_loading;
	bool is_loaded;

    // Caching management.
    // TODO(avirodov): need to rethink this, maybe an external struct that points to isyntax_tile_t.
    bool cache_marked;
    struct isyntax_tile_t* cache_next;
    struct isyntax_tile_t* cache_prev;

    // TODO(avirodov): hide behind compile flag. Useful for debug. Or make proper, useful for marking algorithm too.
    int dbg_tile_scale;
    int dbg_tile_x;
    int dbg_tile_y;
} isyntax_tile_t;

typedef struct isyntax_level_t {
	i32 scale;
	i32 width_in_tiles;
	i32 height_in_tiles;
	float downsample_factor;
	float um_per_pixel_x;
	float um_per_pixel_y;
	float x_tile_side_in_um;
	float y_tile_side_in_um;
	u64 tile_count;
	float origin_offset_in_pixels;
	v2f origin_offset;
	isyntax_tile_t* tiles;
	bool is_fully_loaded;
} isyntax_level_t;

typedef struct isyntax_image_t {
	u32 image_type;
	u8* pixels;
	i32 width;
	i32 height;
	i32 offset_x;
	i32 offset_y;
	i32 level_count;
	i32 max_scale;
	isyntax_level_t levels[16];
	i32 compressor_version;
	bool compression_is_lossy;
	i32 lossy_image_compression_ratio;
	i32 number_of_blocks;
	i32 codeblock_count;
	isyntax_codeblock_t* codeblocks;
	i32 data_chunk_count;
	isyntax_data_chunk_t* data_chunks;
	bool header_codeblocks_are_partial;
	bool first_load_complete;
	bool first_load_in_progress;
} isyntax_image_t;

typedef struct isyntax_parser_node_t {
	u32 node_type; // leaf, branch, or array
	bool has_children;
	bool has_base64_content;
	u16 group;
	u16 element;
} isyntax_parser_node_t;

#define ISYNTAX_MAX_NODE_DEPTH 16

typedef struct isyntax_xml_parser_t {
	yxml_t* x;
	isyntax_image_t* current_image;
	i32 running_image_index;
	u32 current_image_type;
	char* attrbuf;
	char* attrbuf_end;
	char* attrcur;
	size_t attrlen;
	size_t attrbuf_capacity;
	char* contentbuf;
	char* contentcur;
	size_t contentlen;
	size_t contentbuf_capacity;
	char current_dicom_attribute_name[256];
	u32 current_dicom_group_tag;
	u32 current_dicom_element_tag;
	i32 attribute_index;
	u32 current_node_type;
	bool current_node_has_children;
	isyntax_parser_node_t node_stack[ISYNTAX_MAX_NODE_DEPTH];
	i32 node_stack_index;
	isyntax_parser_node_t data_object_stack[ISYNTAX_MAX_NODE_DEPTH];
	i32 data_object_stack_index;
	u32 data_object_flags;
	i32 block_header_template_index;
	i32 cluster_header_template_index;
	i32 block_header_index_for_cluster;
	i32 dimension_index;
	bool initialized;
} isyntax_xml_parser_t;

typedef struct isyntax_t {
	i64 filesize;
	file_handle_t file_handle;
	isyntax_image_t images[16];
	i32 image_count;
	isyntax_block_header_template_t block_header_templates[64];
	i32 block_header_template_count;
	isyntax_cluster_header_template_t cluster_header_templates[8];
	i32 cluster_header_template_count;
	i32 macro_image_index;
	i32 label_image_index;
	i32 wsi_image_index;
	isyntax_xml_parser_t parser;
	float mpp_x;
	float mpp_y;
	bool is_mpp_known;
	i32 block_width;
	i32 block_height;
	i32 tile_width;
	i32 tile_height;
	icoeff_t* black_dummy_coeff;
	icoeff_t* white_dummy_coeff;
	block_allocator_t* ll_coeff_block_allocator;
	block_allocator_t* h_coeff_block_allocator;
    bool32 is_block_allocator_owned;
	float loading_time;
	float total_rgb_transform_time;
	i32 data_model_major_version; // <100 (usually 5) for iSyntax format v1, >= 100 for iSyntax format v2
	work_queue_t* work_submission_queue;
	volatile i32 refcount;
} isyntax_t;

// function prototypes
void isyntax_xml_parser_init(isyntax_xml_parser_t* parser);
bool isyntax_hulsken_decompress(u8 *compressed, size_t compressed_size, i32 block_width, i32 block_height, i32 coefficient, i32 compressor_version, i16* out_buffer);
void isyntax_set_work_queue(isyntax_t* isyntax, work_queue_t* work_queue);
bool isyntax_open(isyntax_t* isyntax, const char* filename, bool init_allocators);
void isyntax_destroy(isyntax_t* isyntax);
void isyntax_idwt(icoeff_t* idwt, i32 quadrant_width, i32 quadrant_height, bool output_steps_as_png, const char* png_name);
u32* isyntax_load_tile(isyntax_t* isyntax, isyntax_image_t* wsi, i32 scale, i32 tile_x, i32 tile_y, block_allocator_t* ll_coeff_block_allocator, bool decode_rgb);
u32 isyntax_get_adjacent_tiles_mask(isyntax_level_t* level, i32 tile_x, i32 tile_y);
u32 isyntax_get_adjacent_tiles_mask_only_existing(isyntax_level_t* level, i32 tile_x, i32 tile_y);
u32 isyntax_idwt_tile_for_color_channel(isyntax_t* isyntax, isyntax_image_t* wsi, i32 scale, i32 tile_x, i32 tile_y, i32 color, icoeff_t* dest_buffer);
void isyntax_decompress_codeblock_in_chunk(isyntax_codeblock_t* codeblock, i32 block_width, i32 block_height, u8* chunk, u64 chunk_base_offset, i32 compressor_version, i16* out_buffer);
i32 isyntax_get_chunk_codeblocks_per_color_for_level(i32 level, bool has_ll);

// TODO: move this somewhere suitable
void isyntax_begin_first_load(i32 resource_id, isyntax_t* isyntax, isyntax_image_t* wsi_image);
void isyntax_begin_load_tile(i32 resource_id, isyntax_t* isyntax, isyntax_image_t* wsi, i32 scale, i32 tile_x, i32 tile_y);

#ifdef __cplusplus
}
#endif
