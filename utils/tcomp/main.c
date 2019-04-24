#include "miniz_common.h"
#include "miniz.h"
#include "cutils.h"

#include <getopt.h>
#include <unistd.h>
#include <sys/stat.h>

/*
 * TComp Image Format:
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
typedef int32_t tint32_t;
typedef uint32_t tuint32_t;

typedef struct {
    tuint32_t package_type_info;
    tuint32_t image_identifier;
    tuint32_t payload_count;
    tuint32_t crc_image;
} tcomp_image_footer_t;

#define TCOMP_TRUE                   1
#define TCOMP_FALSE                  0
#define TCOMP_ADD_MODE_INPUT_LIMIT   8
#define TCOMP_EMBED_MODE_ARG_COUNT   2

#define TCOMP_IMAGE_IDENTIFIER  0xFEEDBABE

#define PAYLOAD_COUNT_UNKNOWN   0x7FFFFFFF

#define FOOTER_SIZE 20

#define tlogf(msg, ...) fprintf(stdout, msg "\n", ##__VA_ARGS__)

typedef struct {
    const char *target;
    tdefl_compressor *compressor;

    FILE *fin;
    FILE *fout;

    tbyte *inp_buf;
    tbyte *out_buf;

    size_t buf_size;
} tcomp_ctx;

static const char *get_full_path(const char *path)
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

int tcomp_init_image(tcomp_ctx *ctx, const char *target)
{
    int result_code = TCOMP_FALSE;

    if (ctx) {
        tlogf("Initializing file descriptors...");

        ctx->fin  = 0;
        ctx->fout = fopen(target, "w+");

        RETURN_ERROR(0 != ctx->fout, TCOMP_FALSE);

        tlogf("Initializing the compressor...");

        ctx->compressor = (tdefl_compressor *) malloc(sizeof(tdefl_compressor));
        /* Return with error if malloc fails! */
        RETURN_ERROR(0 != ctx->compressor, TCOMP_FALSE);

        tlogf("Initializing buffers for compression...");

        ctx->buf_size = 1024 *1024;
        ctx->inp_buf  = (tbyte *) malloc(ctx->buf_size); /* 1 MB */
        ctx->out_buf  = (tbyte *) malloc(ctx->buf_size); /* 1 MB */

        RETURN_ERROR((0 != ctx->inp_buf && 0 != ctx->out_buf), TCOMP_FALSE);

        return TCOMP_TRUE;
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

int tcomp_add_payload(tcomp_ctx *ctx, const char *source, tuint32_t fsize, tuint32_t *out)
{
    int result_code   = TCOMP_FALSE;
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
        RETURN_ERROR(status == TDEFL_STATUS_OKAY, TCOMP_FALSE);

        ctx->fin = fopen(source, "rb");
        RETURN_ERROR(0 != ctx->fin, TCOMP_FALSE);

        while (1) {
           size_t in_bytes, out_bytes;

           if (0 == avail_in) {
              uint32_t n = UTILS_MIN(ctx->buf_size, fsize);
              int r  = fread(ctx->inp_buf, 1, n, ctx->fin);

              RETURN_ERROR(r == n, TCOMP_FALSE);

              next_in = ctx->inp_buf;
              avail_in = n;

              fsize -= n;
           }

           in_bytes = avail_in;
           out_bytes = avail_out;

           status = tdefl_compress(ctx->compressor, next_in, &in_bytes, next_out, &out_bytes, (fsize ? TDEFL_NO_FLUSH : TDEFL_FINISH));

           RETURN_ERROR(status != TDEFL_STATUS_BAD_PARAM, TCOMP_FALSE);

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

              RETURN_ERROR(w == n, TCOMP_FALSE);
              crc_val = (mz_uint32) mz_crc32(crc_val, ctx->out_buf, n);

              next_out = ctx->out_buf;
              avail_out = ctx->buf_size;

              if (status == TDEFL_STATUS_DONE) break;
           }
        }

        RETURN_ERROR(status == TDEFL_STATUS_DONE || status == TDEFL_STATUS_OKAY, TCOMP_FALSE);

        {
           int wr;

           tlogf("Adding payload (%s) - In: %u | Out: %u | CRC 0x%08X", source, total_in, total_out, crc_val);

           wr = fwrite(&total_out, 1, sizeof(total_out), ctx->fout);
           RETURN_ERROR(wr == sizeof(total_out), TCOMP_FALSE);

           wr = fwrite(&crc_val, 1, sizeof(crc_val), ctx->fout);
           RETURN_ERROR(wr == sizeof(crc_val), TCOMP_FALSE);

           if (out) {
               *out = total_out + sizeof(total_out) + sizeof(crc_val);
           }
           result_code = TCOMP_TRUE;
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

int tcomp_finalize_image(tcomp_ctx *ctx, tuint32_t type_info, tuint32_t payload_count, tuint32_t image_size)
{
    int result_code = TCOMP_TRUE;

    if (ctx) {
        tuint32_t identifier = TCOMP_IMAGE_IDENTIFIER;
        mz_uint32 crc_val = MZ_CRC32_INIT;
        int wr, rd;

        wr = fwrite(&type_info, 1, sizeof(type_info), ctx->fout);
        RETURN_ERROR(wr == sizeof(type_info), TCOMP_FALSE);

        wr = fwrite(&identifier, 1, sizeof(identifier), ctx->fout);
        RETURN_ERROR(wr == sizeof(identifier), TCOMP_FALSE);

        wr = fwrite(&payload_count, 1, sizeof(payload_count), ctx->fout);
        RETURN_ERROR(wr == sizeof(identifier), TCOMP_FALSE);

        wr = fwrite(&image_size, 1, sizeof(image_size), ctx->fout);
        RETURN_ERROR(wr == sizeof(image_size), TCOMP_FALSE);

        fflush(ctx->fout);
        fseek(ctx->fout, 0x00, SEEK_SET);

        while ((rd = fread(ctx->inp_buf, 1, ctx->buf_size, ctx->fout)) > 0) {
            crc_val = mz_crc32(crc_val, ctx->inp_buf, rd);
        }

        tlogf("Final CRC value is 0x%08X", crc_val);

        wr = fwrite(&crc_val, 1, sizeof(crc_val), ctx->fout);
        RETURN_ERROR(wr == sizeof(crc_val), TCOMP_FALSE);

        /* The chunk has been successfully written to the file. */
        fclose(ctx->fout);
        ctx->fout = 0;

        result_code = TCOMP_TRUE;
    }

on_exit:
    return result_code;
}

void tcomp_uninit_image(tcomp_ctx *ctx)
{
    if (ctx) {
        if (ctx->fin) fclose(ctx->fin);
        if (ctx->fout) fclose(ctx->fout);
        free(ctx->inp_buf);
        free(ctx->out_buf);
    }
}

int tcomp_validate(const char *source_file, tcomp_image_footer_t *footer)
{
    char buf[4096];
    int result_code = TCOMP_FALSE;
    const char *full_path = get_full_path(source_file);
    tuint32_t crc_read = 0, crc_calculated = MZ_CRC32_INIT;
    tuint32_t package_type_info  = 0;
    tuint32_t image_identifier = 0;
    tuint32_t payload_count = 0;

    int rd = 0;
    int32_t image_size = 0, total_size;

    FILE *fin = fopen(full_path, "r");
    fseek(fin, -1 * FOOTER_SIZE, SEEK_END);

    RETURN_ERROR(ftell(fin) > 0, TCOMP_FALSE);

    rd = fread(&package_type_info, 1, sizeof(package_type_info), fin);
    RETURN_ERROR(rd == sizeof(package_type_info), TCOMP_FALSE);

    rd = fread(&image_identifier, 1, sizeof(image_identifier), fin);
    RETURN_ERROR(rd == sizeof(image_identifier), TCOMP_FALSE);

    rd = fread(&payload_count, 1, sizeof(payload_count), fin);
    RETURN_ERROR(rd == sizeof(payload_count), TCOMP_FALSE);

    rd = fread(&image_size, 1, sizeof(image_size), fin);
    RETURN_ERROR(rd == sizeof(image_size), TCOMP_FALSE);

    //data_size = ftell(fin);

    rd = fread(&crc_read, 1, sizeof(crc_read), fin);
    RETURN_ERROR(rd == sizeof(crc_read), TCOMP_FALSE);

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

    RETURN_ERROR(crc_calculated == crc_read, TCOMP_FALSE);

    tlogf("CRCs matched!");

    if (TCOMP_IMAGE_IDENTIFIER == image_identifier) {
        tlogf("Image identifier is correct!");
    } else {
        tlogf("Image identifier is not correct!");
        RETURN_ERROR(TCOMP_IMAGE_IDENTIFIER == image_identifier, TCOMP_FALSE);
    }

    if (footer) {
        footer->package_type_info = package_type_info;
        footer->image_identifier = image_identifier;
        footer->payload_count = payload_count;
        footer->crc_image = crc_calculated;
    }

    tlogf("Package Type Info: 0x%08X", footer->package_type_info);
    tlogf("!Number of payloads: %d", footer->payload_count);


    result_code = TCOMP_TRUE;

on_exit:
    if (fin) fclose(fin);
    free((void *) full_path);

    return result_code;
}

int load_content(FILE *f, tbyte *buf, tuint32_t size, tuint32_t *read)
{
    int result = TCOMP_FALSE;
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

            result = TCOMP_TRUE;
        }
    }

    return result;
}

int tcomp_load(const char *source_file, tint32_t payload_count, tbyte *images[], tuint32_t image_sizes[])
{
    int result_code = TCOMP_FALSE;
    const char *full_path = 0;
    FILE *fin = 0;
    int nb_image_loaded = 0;

    if (source_file && images && image_sizes) {
        full_path = get_full_path(source_file);
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
                RETURN_ERROR(rd == sizeof(tuint32_t), TCOMP_FALSE);

                rd = fread(&image_crc, 1, sizeof(tuint32_t), fin);
                RETURN_ERROR(rd == sizeof(tuint32_t), TCOMP_FALSE);

                findex = -(image_size + 8);
                fseek(fin, findex, SEEK_CUR);

                images[image_index] = calloc(sizeof(tbyte), image_size);
                tint32_t read_size = 0;
                tuint32_t calc_crc = MZ_CRC32_INIT;
                load_content(fin, images[image_index], image_size, &read_size);

                calc_crc = (mz_uint32) mz_crc32(calc_crc, images[image_index], read_size);

                if (calc_crc != image_crc) {
                    tlogf("CRC mismatch at image index: %d", image_index);
                    RETURN_ERROR(calc_crc == image_crc, TCOMP_FALSE);
                }

                image_sizes[image_index] = read_size;
                ++nb_image_loaded;
                result_code = TCOMP_TRUE;

                findex = -(image_size + 8);
                if (-1 == fseek(fin, findex, SEEK_CUR)) {
                    tlogf("Loading images completed!");
                    break;
                }
            } while (++image_index < TCOMP_ADD_MODE_INPUT_LIMIT);
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

int tcomp_embed(const char *destination_file, const char *source_file)
{
    char buf[4096];
    FILE *fout = fopen(destination_file, "ab");
    FILE *fin = fopen(source_file, "rb");
    int result_code = TCOMP_FALSE;

    if (fout && fin) {
        int r = fseek(fout, 0x00, SEEK_END);
        int w;

        RETURN_ERROR(-1 != r, TCOMP_FALSE);

        while (0 != (r = fread(buf, 1, sizeof(buf), fin))) {
            w = fwrite(buf, 1, r, fout);
            RETURN_ERROR(w == r, TCOMP_FALSE);
        }

        tlogf("Embedding successful!");
        result_code = TCOMP_TRUE;
    }

on_exit:
    if (fout) fclose(fout);
    if (fin) fclose(fin);

    return result_code;
}

int tcomp_create(const char *dest, const char *sources[], tuint32_t *size_info, tuint32_t type_info)
{
    tcomp_ctx ctx;
    tuint32_t output_size, total_size;
    int result = TCOMP_FALSE;

    if (tcomp_init_image(&ctx, dest)) {
        int i;

        total_size = 0;

        for (i = 0; i < TCOMP_ADD_MODE_INPUT_LIMIT; ++i) {
            if (0 == sources[i]) break;

            output_size = 0;
            result = tcomp_add_payload(&ctx, sources[i], size_info[i], &output_size);

            if (TCOMP_TRUE == result) {
                total_size += output_size;
                fflush(ctx.fout);
            } else {
                tlogf("Error while compressing %s!", sources[i]);
                result = TCOMP_FALSE;
                break;
            }
        }

        if (TCOMP_TRUE == result) {
            result = tcomp_finalize_image(&ctx, type_info, i, total_size + FOOTER_SIZE);
            if (TCOMP_TRUE == result) {
                tlogf("Compression successfull! Total image size is %u Bytes.", total_size + FOOTER_SIZE);
            } else {
                tlogf("Image finalization error!");
            }
        }
    }

    tcomp_uninit_image(&ctx);
    return result;
}

int main(int argc, char **argv)
{
    int c, option_index;
    const char *input_files[TCOMP_ADD_MODE_INPUT_LIMIT]          = { 0 };
    tuint32_t  input_files_size_info[TCOMP_ADD_MODE_INPUT_LIMIT] = { 0 };
    const char *embed_mode_files[TCOMP_EMBED_MODE_ARG_COUNT]     = { 0 };
    const char *output_file                                      = 0;
    tuint32_t  image_type_info                                   = 0x00000000;
    int result;

    BOOL creat_mode = FALSE;
    BOOL embed_mode = FALSE;

    struct stat sta;

    static struct option options[] = {
        { "add",      required_argument,  NULL, 'a' },
        { "embed",    required_argument,  NULL, 'e' },
        { "type",     required_argument,  NULL, 't' },
        { "output",   required_argument,  NULL, 'o' },
        { "validate", required_argument,  NULL, 'v' },
        { "help",     no_argument,        NULL, 'h' },
        { NULL },
    };

    for(;;) {
        c = getopt_long_only(argc, argv, "a:e:t:o:h", options, &option_index);
        if (c == -1)
            break;

        switch(c) {
        case 'a':
            {
                int i = 0;

                creat_mode = TRUE;
                --optind;

                for( ;optind < argc && *argv[optind] != '-'; optind++) {

                    if (i >= TCOMP_ADD_MODE_INPUT_LIMIT) {
                        tlogf("Input limit reached! Maximum 8 files can be used to obtain a compressed image.");
                        return -1;
                    }

                    input_files[i] = get_full_path(argv[optind]);

                    if (0 != stat (input_files[i], &sta)) {
                        tlogf("File %s cannot be found! Please provide a valid file path!", argv[optind]);
                        return -2;
                    }

                    input_files_size_info[i] = sta.st_size;
                    tlogf("ADD MODE: Found file %s (%u bytes)", input_files[i], input_files_size_info[i]);
                    ++i;
                }
            }
            break;

        case 'e':
            {
                int i = 0;
                embed_mode = TRUE;
                --optind;

                for( ;optind < argc && *argv[optind] != '-'; optind++) {

                    if (i >= TCOMP_EMBED_MODE_ARG_COUNT) {
                        tlogf("Invalid number of arguments supplied to -e parameter!");
                        return -5;
                    }

                    embed_mode_files[i] = get_full_path(argv[optind]);

                    if (0 != stat (embed_mode_files[i], &sta)) {
                        tlogf("File %s cannot be found! Please provide a valid file path!", argv[optind]);
                        return -6;
                    }

                    ++i;
                    tlogf("EMBED MODE: Found file %s (%zu bytes)", argv[optind], sta.st_size);
                }
            }
            break;

        case 't':
            {
                int ret = sscanf(optarg, "%x", &image_type_info);

                if ((ret <= 0) || (8 < strlen(optarg))) {
                    tlogf("Type info parse failed! Type info should be in 32 bits hexadecimal number format! (%s)", optarg);
                    return -3;
                }

                tlogf("type_info successfully parsed: 0x%08x", image_type_info);
            }
            break;

        case 'o':
            {
                output_file = get_full_path(optarg);

                tlogf("Output file full path is %s", output_file);
            }
            break;

        case 'v':            
            {
                tcomp_image_footer_t footer;

                if (TCOMP_TRUE == tcomp_validate(optarg, &footer)) {
                    tbyte *images[TCOMP_ADD_MODE_INPUT_LIMIT] = { 0 };
                    tuint32_t image_sizes[TCOMP_ADD_MODE_INPUT_LIMIT] = { 0 };

                    tcomp_load(optarg, footer.payload_count, images, image_sizes);
                } else {
                    tlogf("%s is not a valid image!", optarg);
                }
            }
            return 0;

        case '?':
        case 'h':
            tlogf("*****************************************************************");
            tlogf(" -a / --add <file1> <file2> ... <file8>                          ");
            tlogf("    Add files to the image. Each file is compressed before       ");
            tlogf("    appending to the image file.                                 ");
            tlogf(" -o / --output <file>                                            ");
            tlogf("    Specifies target for image creation.                         ");
            tlogf(" -t / --type <32bitHex>                                          ");
            tlogf("    Specifies type identifier for the image creation.            ");
            tlogf(" -e / --embed <so/executable path> <image file path>             ");
            tlogf("    Embeds generated image into a shared object or an executable.");
            tlogf(" -h / --help                                                     ");
            tlogf("    Shows this help message.                                     ");
            tlogf("*****************************************************************");
            break;

        default:
            return 1;
        }
    }

    if (creat_mode && embed_mode) {
        tlogf("-a/dd and -e/mbed arguments cannot be used together in a command line!");
        return -4;
    }

    if (creat_mode) {
        if (0 == output_file) {
            tlogf("No output filename is specified for image generation! (Hint: use -o/--output argument)");
            return -6;
        }

        return tcomp_create(output_file, input_files, input_files_size_info, image_type_info);
    } else if (embed_mode) {
        if (0 == embed_mode_files[1]) {
            tlogf("Output file for embedding mode is missing!");
            return -7;
        }

        return tcomp_embed(embed_mode_files[1], embed_mode_files[0]);
    } else {
        tlogf("Nothing to be done!");
    }

    return 0;
}
