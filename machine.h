/*
 * VM definitions
 * 
 * Copyright (c) 2016-2017 Fabrice Bellard
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

#include "tbvm.h" /* TODO: REMOVE */

#define MAX_DRIVE_DEVICE 4
#define MAX_FS_DEVICE 4
#define MAX_ETH_DEVICE 1

#define VM_CONFIG_VERSION 1

typedef enum {
    VM_FILE_BIOS,
    VM_FILE_VGA_BIOS,
    VM_FILE_KERNEL,

    VM_FILE_COUNT,
} VMFileTypeEnum;

typedef struct {
    char *filename;
    uint8_t *buf;
    int len;
} VMFileEntry;

typedef struct {
    char *device;
    char *filename;
    BlockDevice *block_dev;
} VMDriveEntry;

typedef struct {
    char *device;
    char *tag; /* 9p mount tag */
    char *filename;
    FSDevice *fs_dev;
} VMFSEntry;

typedef struct VirtMachineClass VirtMachineClass;

typedef struct {
    char *cfg_filename;
    const VirtMachineClass *vmc;
    char *machine_name;
    uint64_t ram_size;
    BOOL rtc_real_time;
    BOOL rtc_local_time;
    CharacterDevice *console;
    VMDriveEntry tab_drive[MAX_DRIVE_DEVICE];
    int drive_count;
    VMFSEntry tab_fs[MAX_FS_DEVICE];
    int fs_count;
    char *cmdline; /* bios or kernel command line */
    /* kernel, bios and other auxiliary files */
    VMFileEntry files[VM_FILE_COUNT];
} VirtMachineParams;

typedef struct VirtMachine {
    const VirtMachineClass *vmc;
    /* console */
#ifndef DISABLE_CONSOLE
    VIRTIODevice *console_dev;
    CharacterDevice *console;
#endif
} VirtMachine;


struct VirtMachineClass {
    const char *machine_names;
    void (*virt_machine_set_defaults)(VirtMachineParams *p);
    VirtMachine *(*virt_machine_init)(const VirtMachineParams *p);
    void (*virt_machine_end)(VirtMachine *s);
    int (*virt_machine_get_sleep_duration)(VirtMachine *s, int delay);
    void (*virt_machine_interp)(VirtMachine *s, int max_exec_cycle);
};

extern const VirtMachineClass riscv_machine_class;

void __attribute__((format(printf, 1, 2))) vm_error(const char *fmt, ...);
#ifdef JSON_PARSER
int vm_get_int(JSONValue obj, const char *name, int *pval);
int vm_get_int_opt(JSONValue obj, const char *name, int *pval, int def_val);
#endif
void virt_machine_set_defaults(VirtMachineParams *p);

#ifdef JSON_PARSER
void virt_machine_load_config_file(VirtMachineParams *p,
                                   const char *filename,
                                   void (*start_cb)(void *opaque),
                                   void *opaque);
#else
void virt_machine_set_config(VirtMachineParams *p, const tbvm_init_t *init_args);
#endif

void vm_add_cmdline(VirtMachineParams *p, const char *cmdline);
char *get_file_path(const char *base_filename, const char *filename);
void virt_machine_free_config(VirtMachineParams *p);
VirtMachine *virt_machine_init(const VirtMachineParams *p);
void virt_machine_end(VirtMachine *s);

static inline int virt_machine_get_sleep_duration(VirtMachine *s, int delay)
{
    return s->vmc->virt_machine_get_sleep_duration(s, delay);
}
static inline void virt_machine_interp(VirtMachine *s, int max_exec_cycle)
{
    s->vmc->virt_machine_interp(s, max_exec_cycle);
}

