#include <stdio.h>

#include <getopt.h>
#include <unistd.h>
#include <sys/stat.h>

#include "miniz_common.h"
#include "miniz.h"
#include "cutils.h"
#include "timg.h"

#define TIMG_IMAGE_IDENTIFIER  0xFEEDBABE
#define PAYLOAD_COUNT_UNKNOWN   0x7FFFFFFF
#define FOOTER_SIZE 20

/* ##########################----------------------------------------------------------- */

static int load_content(FILE *f, tbyte *buf, tuint32_t size, tint32_t *read)
{
    int result = TIMG_FALSE;
    int block_size = 1024;
    int rd;

    if (f && buf && read) {
        *read = 0;

        while (size > 0) {
            if (block_size >= size) {
                block_size = size;
            }

            rd = fread(buf + *read, 1, block_size, f);
            *read += rd;
            size -= rd;
            if (rd <= 0) break;

            result = TIMG_TRUE;
        }
    }

    return result;
}

/* ##########################----------------------------------------------------------- */

const char *timg_get_full_path(const char *path)
{
    static const char *working_directory = 0;
    static int working_directory_len = 0;
    char *result;

    if (0 == working_directory) {
        working_directory = getcwd(0, 0);
        if (0 != working_directory) {
            working_directory_len = strlen(working_directory);
        }
    }

    if (path && working_directory) {
        size_t len = strlen(path);

        result = (char *) malloc(len + working_directory_len + 8);
        memset(result, 0x00, len + working_directory_len + 8);

        if ('/' != path[0]) {
            strcpy(result, working_directory);
            strcat(result, "/");
        }

        strcat(result, path);
    }

    return result;
}

/* ##########################----------------------------------------------------------- */

int timg_init_image(timg_ctx *ctx, const char *target)
{
    int result_code = TIMG_FALSE;

    if (ctx) {
        tlogf("Initializing file descriptors...");
        memset(ctx, 0x00, sizeof(*ctx));

        ctx->fin  = 0;
        ctx->fout = fopen(target, "w+");

        RETURN_ERROR(0 != ctx->fout, TIMG_FALSE);

        tlogf("Initializing the compressor...");

        ctx->compressor = (tdefl_compressor *) malloc(sizeof(tdefl_compressor));
        /* Return with error if malloc fails! */
        RETURN_ERROR(0 != ctx->compressor, TIMG_FALSE);

        tlogf("Initializing buffers for compression...");

        ctx->buf_size = 1024 *1024;
        ctx->inp_buf  = (tbyte *) malloc(ctx->buf_size); /* 1 MB */
        ctx->out_buf  = (tbyte *) malloc(ctx->buf_size); /* 1 MB */

        RETURN_ERROR((0 != ctx->inp_buf && 0 != ctx->out_buf), TIMG_FALSE);

        return TIMG_TRUE;
    }

on_exit:
    tlogf("Exiting %s (Code: %d)", __FUNCTION__, result_code);

    if (ctx->fin) fclose(ctx->fin);
    if (ctx->fout) fclose(ctx->fout);

    free(ctx->compressor);
    free(ctx->inp_buf);
    free(ctx->out_buf);

    return result_code;
}

/* ##########################----------------------------------------------------------- */

int timg_add_payload(timg_ctx *ctx, const char *source, tuint32_t fsize, tuint32_t *out)
{
    int result_code   = TIMG_FALSE;
    tuint32_t crc_val = MZ_CRC32_INIT;

    if (ctx && source && fsize > 0) {
        size_t avail_in = 0;
        size_t avail_out = ctx->buf_size;
        uint32_t total_in = 0, total_out = 0;
        tdefl_status status;

        const void *next_in = ctx->inp_buf;
        void *next_out = ctx->out_buf;

        /* Close existing handle */
        if (ctx->fin) {
            fclose(ctx->fin);
            ctx->fin = 0;
        }

        mz_uint comp_flags = TDEFL_WRITE_ZLIB_HEADER | 1500;
        status = tdefl_init(ctx->compressor, NULL, NULL, comp_flags);
        RETURN_ERROR(status == TDEFL_STATUS_OKAY, TIMG_FALSE);

        ctx->fin = fopen(source, "rb");
        RETURN_ERROR(0 != ctx->fin, TIMG_FALSE);

        while (1) {
           size_t in_bytes, out_bytes;

           if (0 == avail_in) {
              uint32_t n = UTILS_MIN(ctx->buf_size, fsize);
              int r  = fread(ctx->inp_buf, 1, n, ctx->fin);

              RETURN_ERROR(r == n, TIMG_FALSE);

              next_in = ctx->inp_buf;
              avail_in = n;

              fsize -= n;
           }

           in_bytes = avail_in;
           out_bytes = avail_out;

           status = tdefl_compress(ctx->compressor, next_in, &in_bytes, next_out, &out_bytes, (fsize ? TDEFL_NO_FLUSH : TDEFL_FINISH));

           RETURN_ERROR(status != TDEFL_STATUS_BAD_PARAM, TIMG_FALSE);

           next_in = (const char *) next_in + in_bytes;
           avail_in -= in_bytes;
           total_in += in_bytes;

           next_out = (char *) next_out + out_bytes;
           avail_out -= out_bytes;
           total_out += out_bytes;

           if ((status != TDEFL_STATUS_OKAY) || (0 == avail_out)) {
               /* Write output buffer to file */

              uint n = ctx->buf_size - avail_out;
              int w  = fwrite(ctx->out_buf, 1, n, ctx->fout);

              RETURN_ERROR(w == n, TIMG_FALSE);
              crc_val = (mz_uint32) mz_crc32(crc_val, ctx->out_buf, n);

              next_out = ctx->out_buf;
              avail_out = ctx->buf_size;

              if (status == TDEFL_STATUS_DONE) break;
           }
        }

        RETURN_ERROR(status == TDEFL_STATUS_DONE || status == TDEFL_STATUS_OKAY, TIMG_FALSE);

        {
           int wr;

           tlogf("Adding payload (%s) - In: %u | Out: %u | CRC 0x%08X", source, total_in, total_out, crc_val);

           wr = fwrite(&total_out, 1, sizeof(total_out), ctx->fout);
           RETURN_ERROR(wr == sizeof(total_out), TIMG_FALSE);

           wr = fwrite(&crc_val, 1, sizeof(crc_val), ctx->fout);
           RETURN_ERROR(wr == sizeof(crc_val), TIMG_FALSE);

           if (out) {
               *out = total_out + sizeof(total_out) + sizeof(crc_val);
           }
           result_code = TIMG_TRUE;
        }
    }

on_exit:
    /* tlogf("Exiting %s for %s (Code: %d)", __FUNCTION__, source, result_code); */

    if (ctx->fin) {
        fclose(ctx->fin);
        ctx->fin = 0;
    }

    return result_code;
}

/* ##########################----------------------------------------------------------- */

tuint8_t* timg_util_decompress_payload(const tuint8_t *input, tuint32_t input_size, unsigned long *output_size)
{
    return tinfl_decompress_mem_to_heap(input, input_size, output_size, TINFL_FLAG_PARSE_ZLIB_HEADER);
}


/* ##########################----------------------------------------------------------- */

int timg_finalize(timg_ctx *ctx, tuint32_t type_info, tuint32_t payload_count, tuint32_t image_size)
{
    int result_code = TIMG_TRUE;

    if (ctx) {
        tuint32_t identifier = TIMG_IMAGE_IDENTIFIER;
        mz_uint32 crc_val = MZ_CRC32_INIT;
        int wr, rd;

        wr = fwrite(&type_info, 1, sizeof(type_info), ctx->fout);
        RETURN_ERROR(wr == sizeof(type_info), TIMG_FALSE);

        wr = fwrite(&identifier, 1, sizeof(identifier), ctx->fout);
        RETURN_ERROR(wr == sizeof(identifier), TIMG_FALSE);

        wr = fwrite(&payload_count, 1, sizeof(payload_count), ctx->fout);
        RETURN_ERROR(wr == sizeof(identifier), TIMG_FALSE);

        wr = fwrite(&image_size, 1, sizeof(image_size), ctx->fout);
        RETURN_ERROR(wr == sizeof(image_size), TIMG_FALSE);

        fflush(ctx->fout);
        fseek(ctx->fout, 0x00, SEEK_SET);

        while ((rd = fread(ctx->inp_buf, 1, ctx->buf_size, ctx->fout)) > 0) {
            crc_val = mz_crc32(crc_val, ctx->inp_buf, rd);
        }

        tlogf("Final CRC value is 0x%08X", crc_val);

        wr = fwrite(&crc_val, 1, sizeof(crc_val), ctx->fout);
        RETURN_ERROR(wr == sizeof(crc_val), TIMG_FALSE);

        /* The chunk has been successfully written to the file. */
        fclose(ctx->fout);
        ctx->fout = 0;

        result_code = TIMG_TRUE;
    }

on_exit:
    return result_code;
}

/* ##########################----------------------------------------------------------- */

void timg_destroy(timg_ctx *ctx)
{
    if (ctx) {
        if (ctx->fin) fclose(ctx->fin);
        if (ctx->fout) fclose(ctx->fout);
        free(ctx->inp_buf);
        free(ctx->out_buf);
        free(ctx->compressor);
    }
}

/* ##########################----------------------------------------------------------- */

int timg_validate(const char *source_file, timg_image_footer_t *footer)
{
    tuint8_t buf[4096];
    int result_code = TIMG_FALSE;
    const char *full_path = timg_get_full_path(source_file);
    tuint32_t crc_read = 0, crc_calculated = MZ_CRC32_INIT;
    tuint32_t package_type_info  = 0;
    tuint32_t image_identifier = 0;
    tuint32_t payload_count = 0;

    int rd = 0;
    int32_t image_size = 0, total_size;

    FILE *fin = fopen(full_path, "r");
    fseek(fin, -1 * FOOTER_SIZE, SEEK_END);

    RETURN_ERROR(ftell(fin) > 0, TIMG_FALSE);

    rd = fread(&package_type_info, 1, sizeof(package_type_info), fin);
    RETURN_ERROR(rd == sizeof(package_type_info), TIMG_FALSE);

    rd = fread(&image_identifier, 1, sizeof(image_identifier), fin);
    RETURN_ERROR(rd == sizeof(image_identifier), TIMG_FALSE);

    rd = fread(&payload_count, 1, sizeof(payload_count), fin);
    RETURN_ERROR(rd == sizeof(payload_count), TIMG_FALSE);

    rd = fread(&image_size, 1, sizeof(image_size), fin);
    RETURN_ERROR(rd == sizeof(image_size), TIMG_FALSE);

    //data_size = ftell(fin);

    rd = fread(&crc_read, 1, sizeof(crc_read), fin);
    RETURN_ERROR(rd == sizeof(crc_read), TIMG_FALSE);

    tlogf("Read CRC of the source file is 0x%08X.", crc_read);
    tlogf("Image size is %d.", image_size);


    fseek(fin, -image_size, SEEK_END);

    do {
        if (0 == (rd = fread(buf, 1, sizeof(buf), fin)))
            break;

        if (rd != sizeof(buf)) {
            rd -= sizeof(crc_read);
        }

        crc_calculated = mz_crc32(crc_calculated, buf, rd);
        image_size -= rd;
        total_size += rd;
    } while (rd > 0 && image_size > 0);

    RETURN_ERROR(crc_calculated == crc_read, TIMG_FALSE);

    tlogf("CRCs matched!");

    if (TIMG_IMAGE_IDENTIFIER == image_identifier) {
        tlogf("Image identifier is correct!");
    } else {
        tlogf("Image identifier is not correct!");
        RETURN_ERROR(TIMG_IMAGE_IDENTIFIER == image_identifier, TIMG_FALSE);
    }

    if (footer) {
        footer->package_type_info = package_type_info;
        footer->image_identifier = image_identifier;
        footer->payload_count = payload_count;
        footer->crc_image = crc_calculated;
    }

    tlogf("Package Type Info: 0x%08X", footer->package_type_info);
    tlogf("!Number of payloads: %d", footer->payload_count);


    result_code = TIMG_TRUE;

on_exit:
    if (fin) fclose(fin);
    free((void *) full_path);

    return result_code;
}

/* ##########################----------------------------------------------------------- */

int timg_load(const char *source_file, tint32_t payload_count, tbyte *images[], tuint32_t image_sizes[])
{
    int result_code = TIMG_FALSE;
    const char *full_path = 0;
    FILE *fin = 0;
    int nb_image_loaded = 0;

    if (source_file && images && image_sizes) {
        full_path = timg_get_full_path(source_file);
        fin = fopen(full_path, "rb");

        if (fin) {
            int image_index = 0;
            int findex = -(8 + FOOTER_SIZE);
            tint32_t  image_size;
            tuint32_t image_crc;
            int rd;

            fseek(fin, findex, SEEK_END);

            do {
                if (payload_count-- <= 0) {
                    /* tlogf("Quitting..."); */
                    break;
                }

                rd = fread(&image_size, 1, sizeof(tuint32_t), fin);
                RETURN_ERROR(rd == sizeof(tuint32_t), TIMG_FALSE);

                rd = fread(&image_crc, 1, sizeof(tuint32_t), fin);
                RETURN_ERROR(rd == sizeof(tuint32_t), TIMG_FALSE);

                findex = -(image_size + 8);
                fseek(fin, findex, SEEK_CUR);

                images[image_index] = calloc(sizeof(tbyte), image_size);
                tint32_t read_size = 0;
                tuint32_t calc_crc = MZ_CRC32_INIT;
                load_content(fin, images[image_index], image_size, &read_size);

                calc_crc = (mz_uint32) mz_crc32(calc_crc, images[image_index], (size_t) read_size);

                if (calc_crc != image_crc) {
                    tlogf("CRC mismatch at image index: %d", image_index);
                    RETURN_ERROR(calc_crc == image_crc, TIMG_FALSE);
                }

                image_sizes[image_index] = read_size;
                ++nb_image_loaded;
                result_code = TIMG_TRUE;

                findex = -(image_size + 8);
                if (-1 == fseek(fin, findex, SEEK_CUR)) {
                    tlogf("Loading images completed!");
                    break;
                }
            } while (++image_index < TIMG_ADD_MODE_INPUT_LIMIT);
        }
    }


on_exit:
    {
        for (int i = 0; i < nb_image_loaded / 2; ++i) {
            tbyte *itmp = images[i];

            images[i] = images[nb_image_loaded - i - 1];
            images[nb_image_loaded - i - 1] = itmp;

            image_sizes[i] ^= image_sizes[nb_image_loaded - i - 1] ^= image_sizes[i] ^= image_sizes[nb_image_loaded - i - 1];
        }
    }

    if (fin) fclose(fin);
    free((void *) full_path);

    return result_code;
}

/* ##########################----------------------------------------------------------- */

int timg_embed(const char *destination_file, const char *source_file)
{
    char buf[4096];
    FILE *fout = fopen(destination_file, "ab");
    FILE *fin = fopen(source_file, "rb");
    int result_code = TIMG_FALSE;

    tlogf("Attempt embedding the image file... %p %p %s", fout, fin, destination_file);
    if (fout && fin) {
        int r = fseek(fout, 0x00, SEEK_END);
        int w;

        RETURN_ERROR(-1 != r, TIMG_FALSE);

        while (0 != (r = fread(buf, 1, sizeof(buf), fin))) {
            w = fwrite(buf, 1, r, fout);
            RETURN_ERROR(w == r, TIMG_FALSE);
        }

        tlogf("Embedding successful!");
        result_code = TIMG_TRUE;
    }

on_exit:
    if (fout) fclose(fout);
    if (fin) fclose(fin);

    return result_code;
}

/* ##########################----------------------------------------------------------- */

int timg_create(const char *dest, const char *sources[], tuint32_t *size_info, tuint32_t type_info)
{
    timg_ctx ctx;
    tuint32_t output_size, total_size;
    int result = TIMG_FALSE;

    if (timg_init_image(&ctx, dest)) {
        int i;

        total_size = 0;

        for (i = 0; i < TIMG_ADD_MODE_INPUT_LIMIT; ++i) {
            if (0 == sources[i]) break;

            output_size = 0;
            result = timg_add_payload(&ctx, sources[i], size_info[i], &output_size);

            if (TIMG_TRUE == result) {
                total_size += output_size;
                fflush(ctx.fout);
            } else {
                tlogf("Error while compressing %s!", sources[i]);
                result = TIMG_FALSE;
                break;
            }
        }

        if (TIMG_TRUE == result) {
            result = timg_finalize(&ctx, type_info, i, total_size + FOOTER_SIZE);
            if (TIMG_TRUE == result) {
                tlogf("Compression successfull! Total image size is %u Bytes.", total_size + FOOTER_SIZE);
            } else {
                tlogf("Image finalization error!");
            }
        }
    }

    timg_destroy(&ctx);
    return result;
}
