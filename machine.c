/*
 * VM utilities
 * 
 * Copyright (c) 2017 Fabrice Bellard
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>

#include "cutils.h"
#include "iomem.h"
#include "virtio.h"
#include "machine.h"

#include "utils/timg/timg.h"
#include "tbvm.h"

void __attribute__((format(printf, 1, 2))) vm_error(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

typedef void FSLoadFileCB(void *opaque, uint8_t *buf, int buf_len);

typedef struct {
    VirtMachineParams *vm_params;
    void (*start_cb)(void *opaque);
    void *opaque;
    
    FSLoadFileCB *file_load_cb;
    void *file_load_opaque;
    int file_index;
} VMConfigLoadState;


static void config_additional_file_load(VMConfigLoadState *s);
static void config_additional_file_load_cb(void *opaque,
                                           uint8_t *buf, int buf_len);

/* XXX: win32, URL */
char *get_file_path(const char *base_filename, const char *filename)
{
    int len, len1;
    char *fname, *p;
    
    if (!base_filename)
        goto done;
    if (strchr(filename, ':'))
        goto done; /* full URL */
    if (filename[0] == '/')
        goto done;
    p = strrchr(base_filename, '/');
    if (!p) {
    done:
        return strdup(filename);
    }
    len = p + 1 - base_filename;
    len1 = strlen(filename);
    fname = malloc(len + len1 + 1);
    memcpy(fname, base_filename, len);
    memcpy(fname + len, filename, len1 + 1);
    return fname;
}

/* return -1 if error. */
static int load_file(uint8_t **pbuf, const char *filename)
{
    FILE *f;
    int size;
    uint8_t *buf;
    
    f = fopen(filename, "rb");
    if (!f) {
        perror(filename);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc(size);
    if (fread(buf, 1, size, f) != size) {
        fprintf(stderr, "%s: read error\n", filename);
        exit(1);
    }
    fclose(f);
    *pbuf = buf;
    return size;
}

static void config_load_file(VMConfigLoadState *s, const char *filename,
                             FSLoadFileCB *cb, void *opaque)
{
    uint8_t *buf;
    int size;
    size = load_file(&buf, filename);
    cb(opaque, buf, size);
    free(buf);
}

void virt_machine_set_config(VirtMachineParams *p, const tbvm_init_t *init_args)
{
    VMConfigLoadState *s;
    const char *filename = init_args->config_path;

    s = tbvm_malloc(sizeof(*s));
    s->vm_params = p;
    s->start_cb = 0;
    s->opaque = 0;
    p->cfg_filename = strdup(filename);

    /* VM Version : 1       */
    /* Machine    : riscv32 */
    p->machine_name = "riscv32";
    p->vmc = &riscv_machine_class;
    p->vmc->virt_machine_set_defaults(p);

    p->cmdline = (char *) init_args->cmdline;

    memset(p->files, 0x00, sizeof(p->files));

    switch (init_args->load_config) {
    case (IMAGE_TYPE_SEPARATE | OS_TYPE_LINUX):
        p->files[VM_FILE_BIOS].filename = (char *) init_args->load_config_data.linux_system.bios_path;
        p->files[VM_FILE_KERNEL].filename = (char *) init_args->load_config_data.linux_system.kernel_path;
        /* TODO: Add multiple disk support back */
        p->tab_drive[0].filename = (char *) init_args->load_config_data.linux_system.disk_image_path;
        p->drive_count = 1;

        break;
    case (IMAGE_TYPE_SEPARATE | OS_TYPE_BAREMETAL):
        p->files[VM_FILE_BIOS].filename = (char *) init_args->load_config_data.baremetal_system.binary_path;

        break;

    case (IMAGE_TYPE_COMBINED | OS_TYPE_BAREMETAL):
    case (IMAGE_TYPE_COMBINED | OS_TYPE_LINUX):
        {
            timg_image_footer_t footer;
            if (TIMG_TRUE == timg_validate(filename, &footer)) {
                tbyte *compressed_images[TIMG_ADD_MODE_INPUT_LIMIT] = { 0 };
                tuint32_t compressed_image_sizes[TIMG_ADD_MODE_INPUT_LIMIT] = { 0 };

                tbyte *decompressed_images[TIMG_ADD_MODE_INPUT_LIMIT] = { 0 };
                tuint32_t decompressed_image_sizes[TIMG_ADD_MODE_INPUT_LIMIT] = { 0 };
                int i;

                timg_load(filename, footer.payload_count, compressed_images, compressed_image_sizes);

                for (i = 0; compressed_images[i] != 0 && i < TIMG_ADD_MODE_INPUT_LIMIT; ++i) {
                    decompressed_images[i] = timg_util_decompress_payload(compressed_images[i], compressed_image_sizes[i], &decompressed_image_sizes[i]);
#if 0
                    {
                        char buf[32];
                        sprintf(buf, "/tmp/%d.bin", i);

                        FILE *f = fopen(buf, "wb");

                        if (f) {
                            fwrite(decompressed_images[i], 1, decompressed_image_sizes[i], f);
                            fclose(f);
                        }
                    }
#endif
                    if (0 == decompressed_images[i]) {
                        fprintf(stdout, "Error while decompressing image! (Index: %d)\n", i + 1);
                        exit(-1);
                    }

                    fprintf(stdout, "%d) Images: %p | Compressed Size: %d | Decompressed Size: %ld\n", i + 1, decompressed_images, compressed_image_sizes[i], decompressed_image_sizes[i]);
                }

                memset(p->files, 0x00, sizeof(p->files));

                p->files[VM_FILE_BIOS].buf = decompressed_images[0];
                p->files[VM_FILE_BIOS].len = decompressed_image_sizes[0];

                if (init_args->load_config & OS_TYPE_LINUX) {
                    p->files[VM_FILE_KERNEL].buf = decompressed_images[1];
                    p->files[VM_FILE_KERNEL].len = decompressed_image_sizes[1];

                    p->files[VM_FILE_ROOTFS].buf = decompressed_images[2];
                    p->files[VM_FILE_ROOTFS].len = decompressed_image_sizes[2];

                    p->tab_drive[0].filename = (char *) init_args->load_config_data.linux_system.disk_image_path;
                    p->drive_count = 1;
                }
            }
        }
        break;
    }

    if (init_args->load_config & IMAGE_TYPE_SEPARATE) {
        s->file_index = 0;
        config_additional_file_load(s);
    }

    if (init_args->load_config & OS_TYPE_LINUX) {
        p->tab_fs[0].tag = (char *) init_args->load_config_data.linux_system.fs_mount_tag;
        p->tab_fs[0].filename = (char *) init_args->load_config_data.linux_system.fs_host_directory;
        p->fs_count = 1;
    }
}

static void config_additional_file_load(VMConfigLoadState *s)
{
    VirtMachineParams *p = s->vm_params;
    while (s->file_index < VM_FILE_COUNT &&
           p->files[s->file_index].filename == NULL) {
        s->file_index++;
    }
    if (s->file_index == VM_FILE_COUNT) {
        if (s->start_cb)
            s->start_cb(s->opaque);
        free(s);
    } else {
        char *fname;
        
        fname = get_file_path(p->cfg_filename,
                              p->files[s->file_index].filename);
        config_load_file(s, fname,
                         config_additional_file_load_cb, s);
        free(fname);
    }
}

static void config_additional_file_load_cb(void *opaque,
                                           uint8_t *buf, int buf_len)
{
    VMConfigLoadState *s = opaque;
    VirtMachineParams *p = s->vm_params;

    p->files[s->file_index].buf = malloc(buf_len);
    memcpy(p->files[s->file_index].buf, buf, buf_len);
    p->files[s->file_index].len = buf_len;

    /* load the next files */
    s->file_index++;
    config_additional_file_load(s);
}

void vm_add_cmdline(VirtMachineParams *p, const char *cmdline)
{
    char *new_cmdline, *old_cmdline;
    if (cmdline[0] == '!') {
        new_cmdline = strdup(cmdline + 1);
    } else {
        old_cmdline = p->cmdline;
        if (!old_cmdline)
            old_cmdline = "";
        new_cmdline = malloc(strlen(old_cmdline) + 1 + strlen(cmdline) + 1);
        strcpy(new_cmdline, old_cmdline);
        strcat(new_cmdline, " ");
        strcat(new_cmdline, cmdline);
    }
    free(p->cmdline);
    p->cmdline = new_cmdline;
}

void virt_machine_free_config(VirtMachineParams *p)
{
    int i;
    
    free(p->cmdline);
    for(i = 0; i < VM_FILE_COUNT; i++) {
        free(p->files[i].filename);
        free(p->files[i].buf);
    }
    for(i = 0; i < p->drive_count; i++) {
        free(p->tab_drive[i].filename);
        free(p->tab_drive[i].device);
    }
    for(i = 0; i < p->fs_count; i++) {
        free(p->tab_fs[i].filename);
        free(p->tab_fs[i].tag);
    }
    free(p->cfg_filename);
}

VirtMachine *virt_machine_init(const VirtMachineParams *p)
{
    const VirtMachineClass *vmc = p->vmc;
    return vmc->virt_machine_init(p);
}

void virt_machine_set_defaults(VirtMachineParams *p)
{
    memset(p, 0, sizeof(*p));
}

void virt_machine_end(VirtMachine *s)
{
    s->vmc->virt_machine_end(s);
}
