/*
 * TBVM initialization interface
 *
 * Copyright (c) 2016 Fabrice Bellard
 * Copyright (c) 2019 Erdem Meydanli
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

#ifndef TBVM_H_INCLUDED
#define TBVM_H_INCLUDED

#define TBVM_SUCCESS                0
#define TBVM_DISK_INIT_ERROR       (-1)
#define TBVM_MACHINE_INIT_ERROR    (-2)
#define TBVM_INVALID_INIT_ARGS     (-3)

#define TBVM_STATE_STOP             0
#define TBVM_STATE_RUN              0xFFFFFFFF

typedef void* tbvm_context_t;

typedef enum {
    OS_TYPE_LINUX,
    OS_TYPE_BAREMETAL,
    OS_TYPE_MAX
} os_type_t;

typedef enum {
    LOADER_TYPE_DYNAMIC,
    LOADER_TYPE_STATIC,
    LOADER_TYPE_MAX
} loader_type_t;

typedef union {
    struct {
        const char *bios_path;
        const char *kernel_path;
        const char *disk_image_path;

        const char *fs_mount_tag;
        const char *fs_host_directory;

        const char *cmdline;
    } os_linux;

    struct {
        const char *binary_path;
    } os_baremetal;
} dynamic_loader_info_t;

typedef union {
    struct {
        void *bios;
        void *kernel;
        void *disk_image;

        const char *fs_mount_tag;
        const char *fs_host_directory;

        const char *cmdline;
    } os_linux;

    struct {
        void *binary;
    } os_baremetal;
} static_loader_info_t;

typedef union {
    static_loader_info_t sinfo;
    dynamic_loader_info_t dinfo;
} loader_info_t;

typedef struct {
    os_type_t os_type;
    int memory_size;
    int allow_ctrlc;
    loader_type_t loader_type;
    loader_info_t loader_info;
    const char *config_path;
} tbvm_init_t;

void tbvm_get_default_init_arguments(tbvm_init_t *init_args);
tbvm_context_t tbvm_init(const tbvm_init_t *init_args, int *err);

void tbvm_event_loop(tbvm_context_t ctx);
void tbvm_run(tbvm_context_t ctx, volatile int *stop_flag, int msec_delay);

void tbvm_uninit(tbvm_context_t ctx);

const char *tbvm_get_version_info();
const char *tbvm_get_build_info();

#endif /* TBVM_H_INCLUDED */
