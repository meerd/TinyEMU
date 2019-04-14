#include <stdint.h>
#include <stdio.h>
#include <limits.h>

#include "miniz.h"

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
 *  | Package Typer (4 bytes)    |
 *  | Image Identifier (4 bytes) |
 *  | CRC32 ALL (4 bytes)        |
 *  ------------------------------
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <getopt.h>
#include "miniz.h"
#include "cutils.h"

typedef unsigned char tbyte;
typedef unsigned long tsize_t;
typedef uint32_t tuint32_t;

#define TCOMP_TRUE                   1
#define TCOMP_FALSE                  0
#define TCOMP_ADD_MODE_INPUT_LIMIT   8
#define TCOMP_EMBED_MODE_ARG_COUNT   2

#define TCOMP_IMAGE_IDENTIFIER  0xFEEDBABE

#define tlogf(msg, ...) fprintf(stdout, msg "\n", ##__VA_ARGS__)

typedef struct {
    const char *target;
    tdefl_compressor *compressor;

    FILE *fin;
    FILE *fout;

    tbyte *inp_buf;
    tbyte *out_buf;

    tuint32_t buf_size;
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
            if ('.' == path[0]) {
                strcat(result, "/");
            }
        }

        strcat(result, path);
    }

    return result;
}

int tcomp_init_image(tcomp_ctx *ctx, const char *target)
{
    static const mz_uint s_tdefl_num_probes[11] = { 0, 1, 6, 32,  16, 32, 128, 256,  512, 768, 1500 };
    int result_code = TCOMP_FALSE;

    if (ctx) {
        mz_uint comp_flags = TDEFL_WRITE_ZLIB_HEADER | s_tdefl_num_probes[10];

        tlogf("Initializing file descriptors...");

        ctx->fin  = 0;
        ctx->fout = fopen(target, "wb");

        RETURN_ERROR(0 != ctx->fout, TCOMP_FALSE);

        tlogf("Initializing the compressor...");

        ctx->compressor = (tdefl_compressor *) malloc(sizeof(tdefl_compressor));
        /* Return with error if malloc fails! */
        RETURN_ERROR(0 != ctx->compressor, TCOMP_FALSE);

        tdefl_status status = tdefl_init(ctx->compressor, NULL, NULL, comp_flags);
        /* Return with error if compressor initialization fails! */
        RETURN_ERROR(status == TDEFL_STATUS_OKAY, TCOMP_FALSE);

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

#if 0
int tcomp_expand_buf_size(tcomp_ctx *ctx, tsize_t requested_size)
{
    if (ctx) {
        tsize_t ptr_index = (ctx->ptr - ctx->buf);

        if (requested_size > (ctx->size - ptr_index)) {
            /* Double buffer size */
            tsize_t new_size = ctx->size * 2;

            tlogf("Expanding buffer! Requested: %zu, we have: %zu.", requested_size, (ctx->size - ptr_index));

            /* Still not enough to fit in? */
            if (requested_size > (new_size - ptr_index)) {
                new_size += requested_size;
            }

            tlogf("New buffer size is %zu.", new_size);

            tbyte *new_addr = realloc(ctx->buf, new_size);

            if (new_addr) {
                ctx->buf = new_addr;
                ctx->ptr = ctx->buf + ptr_index;
                return TCOMP_TRUE;
            }
        }

        return TCOMP_TRUE;
    }

    return TCOMP_FALSE;
}
#endif

int tcomp_add_payload(tcomp_ctx *ctx, const char *source, tsize_t fsize)
{
    int result_code   = TCOMP_FALSE;
    tuint32_t crc_val = MZ_CRC32_INIT;

    if (ctx && source && fsize > 0) {
        tuint32_t avail_in = 0;
        tuint32_t avail_out = ctx->buf_size;
        tuint32_t total_in = 0, total_out = 0;
        tdefl_status status;

        const void *next_in = ctx->inp_buf;
        void *next_out = ctx->out_buf;

        /* Close existing handle */
        if (ctx->fin) {
            fclose(ctx->fin);
            ctx->fin = 0;
        }

        ctx->fin = fopen(source, "rb");
        RETURN_ERROR(0 != ctx->fin, TCOMP_FALSE);

        while (1) {
           tsize_t in_bytes, out_bytes;

           if (0 == avail_in) {
              // Input buffer is empty, so read more bytes from input file.
              uint n = UITLS_MIN(ctx->buf_size, fsize);
              int r  = fread(ctx->inp_buf, 1, n, ctx->fin);

              RETURN_ERROR(r == n, TCOMP_FALSE);

              next_in = ctx->inp_buf;
              avail_in = n;

              fsize -= n;
           }

           in_bytes = avail_in;
           out_bytes = avail_out;
           // Compress as much of the input as possible (or all of it) to the output buffer.
           tlogf("FSIZE: %d", fsize);
           status = tdefl_compress(ctx->compressor, next_in, &in_bytes, next_out, &out_bytes, fsize ? TDEFL_NO_FLUSH : TDEFL_FINISH);
           tlogf("STATUS: %d", status);

           next_in = (const char *) next_in + in_bytes;
           avail_in -= in_bytes;
           total_in += in_bytes;

           next_out = (char *) next_out + out_bytes;
           avail_out -= out_bytes;
           total_out += out_bytes;

           tlogf("Next In: %u | Next Out: %u", in_bytes, out_bytes);

           if ((status != TDEFL_STATUS_OKAY) || (0 != avail_out)) {
               /* Write output buffer to file */
               tlogf("Buf size: %d | Avail out: %d", ctx->buf_size, avail_out);

              uint n = ctx->buf_size - (uint) avail_out;
              int w  = fwrite(ctx->out_buf, 1, n, ctx->fout);
              crc_val = (mz_uint32) mz_crc32(crc_val, ctx->out_buf, n);

              RETURN_ERROR(w == n, TCOMP_FALSE);

              next_out = ctx->out_buf;
              avail_out = ctx->buf_size;
           }

        }

        tlogf("Adding payload (%s) - In: %d | Out: %d", source, total_in, total_out);

        RETURN_ERROR(status == TDEFL_STATUS_DONE || status == TDEFL_STATUS_OKAY, TCOMP_FALSE);

        if (status == TDEFL_STATUS_DONE) {
           int wr;

           wr = fwrite(&total_out, 1, sizeof(total_out), ctx->fout);
           RETURN_ERROR(wr == sizeof(total_out), TCOMP_FALSE);

           wr = fwrite(crc32, 1, sizeof(crc32), ctx->fout);
           RETURN_ERROR(wr == sizeof(crc32), TCOMP_FALSE);

           tlogf("Payload (%s) successfully added!", source, total_in, total_out);

           /* The chunk has been successfully written to the file. */
           result_code = TCOMP_TRUE;
        }

        exit(0);
     }


on_exit:
    tlogf("Exiting %s (Code: %d)", __FUNCTION__, result_code);

    if (ctx->fin) {
        fclose(ctx->fin);
        ctx->fin = 0;
    }

    return result_code;
}

int tcomp_finalize_image(tcomp_ctx *ctx)
{
    if (ctx) {
#if 0
        tuint32_t identifier = TCOMP_IMAGE_IDENTIFIER;
        tuint32_t crc;
        tuint32_t requested_size = sizeof(identifier) + sizeof(crc);

        if (TCOMP_TRUE == tcomp_expand_buf_size(ctx, requested_size)) {

            /* Append payload */
            memcpy(ctx->ptr, &identifier, sizeof(identifier));
            ctx->ptr += sizeof(identifier);

            /* Append CRC32 of payload + size */
            crc = (mz_uint32) mz_crc32(MZ_CRC32_INIT, ctx->buf, (ctx->ptr - ctx->buf));
            memcpy(ctx->ptr, &crc, sizeof(crc));
            ctx->ptr += sizeof(crc);

            return TCOMP_TRUE;
        }
#endif
    }

    tlogf("Error while adding payload!");

    return TCOMP_FALSE;
}


int tcomp_embed(const char *destination_file, const char *source_file)
{

}

int tcomp_create(const char *dest, const char *sources[], tuint32_t *size_info[], tuint32_t type_info)
{
    tcomp_ctx ctx;
    int retval = TCOMP_FALSE;

    if (tcomp_init_image(&ctx, dest)) {
        int i;

        for (i = 0; i < TCOMP_ADD_MODE_INPUT_LIMIT; ++i) {
            if (0 == sources[i]) break;

            tcomp_add_payload(&ctx, sources[i], size_info[i]);
        }
    }
}

int main(int argc, char **argv)
{
    int c, option_index;
    const char *input_files[TCOMP_ADD_MODE_INPUT_LIMIT]          = { 0 };
    tuint32_t  input_files_size_info[TCOMP_ADD_MODE_INPUT_LIMIT] = { 0 };
    const char *embed_mode_files[TCOMP_EMBED_MODE_ARG_COUNT]     = { 0 };
    const char *output_file                                      = 0;
    tuint32_t  image_type_info                                   = 0x00000000;

    BOOL creat_mode = FALSE;
    BOOL embed_mode = FALSE;

    struct stat sta;

    static struct option options[] = {
        { "add",    required_argument,  NULL, 'a' },
        { "embed",  required_argument,  NULL, 'e' },
        { "type",   required_argument,  NULL, 't' },
        { "output", required_argument,  NULL, 'o' },
        { "help",   no_argument,        NULL, 'h' },
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
                    tlogf("ADD MODE: Found file %s (%zu bytes)", argv[optind], input_files_size_info[i]);
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

#if 0

   else if ((pMode[0] == 'd') || (pMode[0] == 'D'))
   {
      // Decompression.
      uint infile_remaining = infile_size;

      tinfl_decompressor inflator;
      tinfl_init(&inflator);

      for ( ; ; )
      {
         size_t in_bytes, out_bytes;
         tinfl_status status;
         if (!avail_in)
         {
            // Input buffer is empty, so read more bytes from input file.
            uint n = my_min(IN_BUF_SIZE, infile_remaining);

            if (fread(s_inbuf, 1, n, pInfile) != n)
            {
               printf("Failed reading from input file!\n");
               return EXIT_FAILURE;
            }

            next_in = s_inbuf;
            avail_in = n;

            infile_remaining -= n;
         }

         in_bytes = avail_in;
         out_bytes = avail_out;
         status = tinfl_decompress(&inflator, (const mz_uint8 *)next_in, &in_bytes, s_outbuf, (mz_uint8 *)next_out, &out_bytes, (infile_remaining ? TINFL_FLAG_HAS_MORE_INPUT : 0) | TINFL_FLAG_PARSE_ZLIB_HEADER);

         avail_in -= in_bytes;
         next_in = (const mz_uint8 *)next_in + in_bytes;
         total_in += in_bytes;

         avail_out -= out_bytes;
         next_out = (mz_uint8 *)next_out + out_bytes;
         total_out += out_bytes;

         if ((status <= TINFL_STATUS_DONE) || (!avail_out))
         {
            // Output buffer is full, or decompression is done, so write buffer to output file.
            uint n = OUT_BUF_SIZE - (uint)avail_out;
            if (fwrite(s_outbuf, 1, n, pOutfile) != n)
            {
               printf("Failed writing to output file!\n");
               return EXIT_FAILURE;
            }
            next_out = s_outbuf;
            avail_out = OUT_BUF_SIZE;
         }

         // If status is <= TINFL_STATUS_DONE then either decompression is done or something went wrong.
         if (status <= TINFL_STATUS_DONE)
         {
            if (status == TINFL_STATUS_DONE)
            {
               // Decompression completed successfully.
               break;
            }
            else
            {
               // Decompression failed.
               printf("tinfl_decompress() failed with status %i!\n", status);
               return EXIT_FAILURE;
            }
         }
      }
   }
   else
   {
      printf("Invalid mode!\n");
      return EXIT_FAILURE;
   }

   fclose(pInfile);
   if (EOF == fclose(pOutfile))
   {
      printf("Failed writing to output file!\n");
      return EXIT_FAILURE;
   }

   printf("Total input bytes: %u\n", (mz_uint32)total_in);
   printf("Total output bytes: %u\n", (mz_uint32)total_out);
   printf("Success.\n");
   return EXIT_SUCCESS;
}
#endif
