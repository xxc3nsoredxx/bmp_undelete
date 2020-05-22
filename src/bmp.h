#ifndef BMP_H_20191109_000920
#define BMP_H_20191109_000920

#include <stdint.h>

extern const char BMP_MAGIC [2];

struct bmp_head_s {
    uint8_t  bmp_magic [2];
    uint32_t bmp_file_size;
    uint16_t bmp_res1;
    uint16_t bmp_res2;
    uint32_t bmp_pixel_off;
} __attribute__((packed));

struct dib_head_s {
    uint32_t dib_size;
    int32_t  dib_width;
    int32_t  dib_height;
    uint16_t dib_planes;
    uint16_t dib_bpp;
    uint32_t dib_comp_method;
    uint32_t dib_image_size;
    int32_t  dib_horiz_ppm;
    int32_t  dib_vert_ppm;
    uint32_t dib_cols_in_palette;
    uint32_t dib_import_cols;
} __attribute__((packed));

#endif /* BMP_H_20191109_000920 */
