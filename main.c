#include <stdio.h>
#include "getopt.h"

#include "tbvm.h"

static struct option options[] = {
    { "help", no_argument, NULL, 'h' },
    { "ctrlc", no_argument },
    { "rw", no_argument },
    { "ro", no_argument },
    { "append", required_argument },
    { "no-accel", no_argument },
    { NULL },
};

int main(int argc, char **argv)
{
    tbvm_init_t init_args;
    int c, option_index, err;
    tbvm_context_t ctx;

    fprintf(stdout, "Tiny-bang Virtual Machine %s - %s\n", tbvm_get_version_info(), tbvm_get_build_info());

    tbvm_get_default_init_arguments(&init_args);
    ctx = tbvm_init(&init_args, &err);

    if (0 == ctx) {
        fprintf(stderr, "Error while iniatlizin virtual machine. (Code: %d)\n", err);
        return -1;
    }

    fprintf(stdout, "Attempt running virtual machine...\n");

    tbvm_run(ctx, 0, 0);

    for(;;) {
        c = getopt_long_only(argc, argv, "m:", options, &option_index);
        if (c == -1)
            break;

        switch(c) {
        default:
            return 1;
        }
    }

    return 0;
}
