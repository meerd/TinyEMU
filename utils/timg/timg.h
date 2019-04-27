#ifndef TIMG_H_INCLUDED
#define TIMG_H_INCLUDED

#include <stdint.h>

/*
 * timg Image Format:
 *  ------------------------------
 *  | Payload 1                  |
 *  | Payload 1 Size (4 bytes)   |
 *  | CRC32 (4 bytes)            |
 *  | Payload 2                  |
 *  | Payload 2 Size (4 bytes)   |
 *  | CRC32 (4 bytes)            |
 *  | ...                        |
 *  | Payload n                  |
 *  | Payload n Size (4 bytes)   |
 *  | CRC32 (4 bytes)            |
 *  |----------------------------|
 *  | Package Type Info(4 bytes) |
 *  | Image Identifier (4 bytes) |
 *  | Payload Count (4 bytes)    |
 *  | Image Size (4 bytes)       |
 *  | CRC32 ALL (4 bytes)        |
 *  ------------------------------
 */

typedef unsigned char tbyte;
typedef size_t        tsize_t;

typedef uint8_t       tuint8_t;
typedef uint32_t      tuint32_t;
typedef int32_t       tint32_t;

typedef struct {
    tuint32_t package_type_info;
    tuint32_t image_identifier;
    tuint32_t payload_count;
    tuint32_t crc_image;
} timg_image_footer_t;

typedef struct {
    const char *target;
    void *compressor;

    void *fin;
    void *fout;

    tbyte *inp_buf;
    tbyte *out_buf;

    size_t buf_size;
} timg_ctx;

#define TIMG_TRUE                   1
#define TIMG_FALSE                  0

#define TIMG_ADD_MODE_INPUT_LIMIT   8
#define TIMG_EMBED_MODE_ARG_COUNT   2

int  timg_create(const char *dest, const char *sources[], tuint32_t *size_info, tuint32_t type_info);

int  timg_init_image(timg_ctx *ctx, const char *target);
int  timg_add_payload(timg_ctx *ctx, const char *source, tuint32_t fsize, tuint32_t *out);
int  timg_finalize(timg_ctx *ctx, tuint32_t type_info, tuint32_t payload_count, tuint32_t image_size);

int  timg_validate(const char *source_file, timg_image_footer_t *footer);
int  timg_load(const char *source_file, tint32_t payload_count, tbyte *images[], tuint32_t image_sizes[]);
int  timg_embed(const char *destination_file, const char *source_file);

void timg_destroy(timg_ctx *ctx);

const char *timg_get_full_path(const char *path);
tuint8_t* timg_util_decompress_payload(const tuint8_t *input, tuint32_t input_size, unsigned long *output_size);

#define tlogf(msg, ...) fprintf(stdout, msg "\n", ##__VA_ARGS__)

#endif /* TIMG_H_INCLUDED */
