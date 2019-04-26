#include <stdio.h>

#include <unistd.h>
#include <sys/stat.h>
#include <getopt.h>

#include "timg.h"

int main(int argc, char **argv)
{
    int c, option_index;
    const char *input_files[TIMG_ADD_MODE_INPUT_LIMIT]          = { 0 };
    tuint32_t  input_files_size_info[TIMG_ADD_MODE_INPUT_LIMIT] = { 0 };
    const char *embed_mode_files[TIMG_EMBED_MODE_ARG_COUNT]     = { 0 };
    const char *output_file                                      = 0;
    tuint32_t  image_type_info                                   = 0x00000000;
    int result;

    int creat_mode = 0;
    int embed_mode = 0;

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

                creat_mode = 1;
                --optind;

                for( ;optind < argc && *argv[optind] != '-'; optind++) {

                    if (i >= TIMG_ADD_MODE_INPUT_LIMIT) {
                        tlogf("Input limit reached! Maximum 8 files can be used to obtain a compressed image.");
                        return -1;
                    }

                    input_files[i] = timg_get_full_path(argv[optind]);

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
                embed_mode = 1;
                --optind;

                for( ;optind < argc && *argv[optind] != '-'; optind++) {

                    if (i >= TIMG_EMBED_MODE_ARG_COUNT) {
                        tlogf("Invalid number of arguments supplied to -e parameter!");
                        return -5;
                    }

                    embed_mode_files[i] = timg_get_full_path(argv[optind]);

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
                output_file = timg_get_full_path(optarg);

                tlogf("Output file full path is %s", output_file);
            }
            break;

        case 'v':            
            {
                timg_image_footer_t footer;

                if (TIMG_TRUE == timg_validate(optarg, &footer)) {
                    tbyte *images[TIMG_ADD_MODE_INPUT_LIMIT] = { 0 };
                    tuint32_t image_sizes[TIMG_ADD_MODE_INPUT_LIMIT] = { 0 };

                    timg_load(optarg, footer.payload_count, images, image_sizes);
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

        return timg_create(output_file, input_files, input_files_size_info, image_type_info);
    } else if (embed_mode) {
        if (0 == embed_mode_files[1]) {
            tlogf("Output file for embedding mode is missing!");
            return -7;
        }

        return timg_embed(embed_mode_files[1], embed_mode_files[0]);
    } else {
        tlogf("Nothing to be done!");
    }

    return 0;
}
