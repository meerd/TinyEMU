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
#include <getopt.h>

#include <termios.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <linux/if_tun.h>

#include <sys/stat.h>
#include <signal.h>

#include "cutils.h"
#include "iomem.h"
#include "virtio.h"
#include "machine.h"
#include "tbvm.h"

#define TBVM_MAJOR_VERSION      0
#define TBVM_MINOR_VERSION      1
#define TBVM_PATCH_VERSION      0

typedef struct {
    int stdin_fd;
    int console_esc_state;
    BOOL resize_pending;
} STDIODevice;

static char version_info[12] = { 0 };

#ifndef DISABLE_CONSOLE
static struct termios oldtty;
static int old_fd0_flags;
static STDIODevice *global_stdio_device;

static void term_exit(void)
{
    tcsetattr (0, TCSANOW, &oldtty);
    fcntl(0, F_SETFL, old_fd0_flags);
}

static void term_init(BOOL allow_ctrlc)
{
    struct termios tty;

    memset(&tty, 0, sizeof(tty));
    tcgetattr (0, &tty);
    oldtty = tty;
    old_fd0_flags = fcntl(0, F_GETFL);

    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
                          |INLCR|IGNCR|ICRNL|IXON);
    tty.c_oflag |= OPOST;
    tty.c_lflag &= ~(ECHO|ECHONL|ICANON|IEXTEN);
    if (!allow_ctrlc)
        tty.c_lflag &= ~ISIG;
    tty.c_cflag &= ~(CSIZE|PARENB);
    tty.c_cflag |= CS8;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 0;

    tcsetattr (0, TCSANOW, &tty);

    atexit(term_exit);
}

static void console_write(void *opaque, const uint8_t *buf, int len)
{
    fwrite(buf, 1, len, stdout);
    fflush(stdout);
}

static int console_read(void *opaque, uint8_t *buf, int len)
{
    STDIODevice *s = opaque;
    int ret, i, j;
    uint8_t ch;
    
    if (len <= 0)
        return 0;

    ret = read(s->stdin_fd, buf, len);
    if (ret < 0)
        return 0;
    if (ret == 0) {
        /* EOF */
        exit(1);
    }

    j = 0;
    for(i = 0; i < ret; i++) {
        ch = buf[i];
        if (s->console_esc_state) {
            s->console_esc_state = 0;
            switch(ch) {
            case 'x':
                printf("Terminated\n");
                exit(0);
            case 'h':
                printf("\n"
                       "C-a h   print this help\n"
                       "C-a x   exit emulator\n"
                       "C-a C-a send C-a\n"
                       );
                break;
            case 1:
                goto output_char;
            default:
                break;
            }
        } else {
            if (ch == 1) {
                s->console_esc_state = 1;
            } else {
            output_char:
                buf[j++] = ch;
            }
        }
    }
    return j;
}

static void term_resize_handler(int sig)
{
    if (global_stdio_device)
        global_stdio_device->resize_pending = TRUE;
}

static void console_get_size(STDIODevice *s, int *pw, int *ph)
{
    struct winsize ws;
    int width, height;
    /* default values */
    width = 80;
    height = 25;
    if (ioctl(s->stdin_fd, TIOCGWINSZ, &ws) == 0 &&
        ws.ws_col >= 4 && ws.ws_row >= 4) {
        width = ws.ws_col;
        height = ws.ws_row;
    }
    *pw = width;
    *ph = height;
}

CharacterDevice *console_init(BOOL allow_ctrlc)
{
    CharacterDevice *dev;
    STDIODevice *s;
    struct sigaction sig;

    term_init(allow_ctrlc);

    dev = tbvm_malloc(sizeof(*dev));
    s = tbvm_malloc(sizeof(*s));
    s->stdin_fd = 0;
    /* Note: the glibc does not properly tests the return value of
       write() in printf, so some messages on stdout may be lost */
    fcntl(s->stdin_fd, F_SETFL, O_NONBLOCK);

    s->resize_pending = TRUE;
    global_stdio_device = s;
    
    /* use a signal to get the host terminal resize events */
    sig.sa_handler = term_resize_handler;
    sigemptyset(&sig.sa_mask);
    sig.sa_flags = 0;
    sigaction(SIGWINCH, &sig, NULL);
    
    dev->opaque = s;
    dev->write_data = console_write;
    dev->read_data = console_read;
    return dev;
}
#endif

typedef enum {
    BF_MODE_RO,
    BF_MODE_RW,
    BF_MODE_SNAPSHOT,
} BlockDeviceModeEnum;

#define SECTOR_SIZE 512

typedef struct BlockDeviceFile {
    FILE *f;
    int64_t nb_sectors;
    BlockDeviceModeEnum mode;
    uint8_t **sector_table;
} BlockDeviceFile;

static int64_t bf_get_sector_count(BlockDevice *bs)
{
    BlockDeviceFile *bf = bs->opaque;
    return bf->nb_sectors;
}

//#define DUMP_BLOCK_READ

static int bf_read_async(BlockDevice *bs,
                         uint64_t sector_num, uint8_t *buf, int n,
                         BlockDeviceCompletionFunc *cb, void *opaque)
{
    BlockDeviceFile *bf = bs->opaque;
    //    printf("bf_read_async: sector_num=%" PRId64 " n=%d\n", sector_num, n);
#ifdef DUMP_BLOCK_READ
    {
        static FILE *f;
        if (!f)
            f = fopen("/tmp/read_sect.txt", "wb");
        fprintf(f, "%" PRId64 " %d\n", sector_num, n);
    }
#endif
    int retval; /* Suppresses the warning. */

    if (!bf->f)
        return -1;
    if (bf->mode == BF_MODE_SNAPSHOT) {
        int i;
        (void) retval; /* Suppresses the warning. */
        for(i = 0; i < n; i++) {
            if (!bf->sector_table[sector_num]) {
                fseek(bf->f, sector_num * SECTOR_SIZE, SEEK_SET);
                retval = fread(buf, 1, SECTOR_SIZE, bf->f);
            } else {
                memcpy(buf, bf->sector_table[sector_num], SECTOR_SIZE);
            }
            sector_num++;
            buf += SECTOR_SIZE;
        }
    } else {
        fseek(bf->f, sector_num * SECTOR_SIZE, SEEK_SET);
        retval = fread(buf, 1, n * SECTOR_SIZE, bf->f);
    }
    /* synchronous read */
    return 0;
}

static int bf_write_async(BlockDevice *bs,
                          uint64_t sector_num, const uint8_t *buf, int n,
                          BlockDeviceCompletionFunc *cb, void *opaque)
{
    BlockDeviceFile *bf = bs->opaque;
    int ret;

    switch(bf->mode) {
    case BF_MODE_RO:
        ret = -1; /* error */
        break;
    case BF_MODE_RW:
        fseek(bf->f, sector_num * SECTOR_SIZE, SEEK_SET);
        fwrite(buf, 1, n * SECTOR_SIZE, bf->f);
        ret = 0;
        break;
    case BF_MODE_SNAPSHOT:
        {
            int i;
            if ((sector_num + n) > bf->nb_sectors)
                return -1;
            for(i = 0; i < n; i++) {
                if (!bf->sector_table[sector_num]) {
                    bf->sector_table[sector_num] = malloc(SECTOR_SIZE);
                }
                memcpy(bf->sector_table[sector_num], buf, SECTOR_SIZE);
                sector_num++;
                buf += SECTOR_SIZE;
            }
            ret = 0;
        }
        break;
    default:
        abort();
    }

    return ret;
}

static BlockDevice *block_device_init(const char *filename,
                                      BlockDeviceModeEnum mode)
{
    BlockDevice *bs;
    BlockDeviceFile *bf;
    int64_t file_size;
    FILE *f;
    const char *mode_str;

    if (mode == BF_MODE_RW) {
        mode_str = "r+b";
    } else {
        mode_str = "rb";
    }
    
    f = fopen(filename, mode_str);
    if (!f) {
        perror(filename);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    file_size = ftello(f);

    bs = tbvm_malloc(sizeof(*bs));
    bf = tbvm_malloc(sizeof(*bf));

    bf->mode = mode;
    bf->nb_sectors = file_size / 512;
    bf->f = f;

    if (mode == BF_MODE_SNAPSHOT) {
        bf->sector_table = tbvm_malloc(sizeof(bf->sector_table[0]) *
                                   bf->nb_sectors);
    }
    
    bs->opaque = bf;
    bs->get_sector_count = bf_get_sector_count;
    bs->read_async = bf_read_async;
    bs->write_async = bf_write_async;
    return bs;
}

#define MAX_EXEC_CYCLE 500000
#define MAX_SLEEP_TIME 10 /* in ms */

static void virt_machine_run(VirtMachine *m)
{
    fd_set rfds, wfds, efds;
    int fd_max, delay, ret;
    struct timeval tv;
#ifndef DISABLE_CONSOLE
    int stdin_fd;
#endif

    delay = virt_machine_get_sleep_duration(m, MAX_SLEEP_TIME);

    /* wait for an event */
    FD_ZERO(&rfds);
    FD_ZERO(&wfds);
    FD_ZERO(&efds);
    fd_max = -1;

#ifndef DISABLE_CONSOLE
    if (m->console_dev && virtio_console_can_write_data(m->console_dev)) {
        STDIODevice *s = m->console->opaque;
        stdin_fd = s->stdin_fd;
        FD_SET(stdin_fd, &rfds);
        fd_max = stdin_fd;

        if (s->resize_pending) {
            int width, height;
            console_get_size(s, &width, &height);
            virtio_console_resize_event(m->console_dev, width, height);
            s->resize_pending = FALSE;
        }
    }
#endif

    tv.tv_sec = delay / 1000;
    tv.tv_usec = (delay % 1000) * 1000;
    ret = select(fd_max + 1, &rfds, &wfds, &efds, &tv);

#ifndef DISABLE_CONSOLE
    if (ret > 0) {
        if (m->console_dev && FD_ISSET(stdin_fd, &rfds)) {
            uint8_t buf[128];
            int ret, len;
            len = virtio_console_get_write_len(m->console_dev);
            len = min_int(len, sizeof(buf));
            ret = m->console->read_data(m->console->opaque, buf, len);
            if (ret > 0) {
                virtio_console_write_data(m->console_dev, buf, ret);
            }
        }
    }
#else
    (void) ret;
#endif

    virt_machine_interp(m, MAX_EXEC_CYCLE);
}

/*******************************************************/

void tbvm_get_default_init_arguments(tbvm_init_t *init_args)
{
    if (init_args) {
        const char *wd = getcwd(0, 0);

        if (wd) {
            int wd_len = strlen(wd);

            init_args->os_type     = OS_TYPE_LINUX;
            init_args->memory_size = 16; /* MB */
            init_args->allow_ctrlc = 1; /* Allow CTRL + C */
            init_args->loader_type = LOADER_TYPE_DYNAMIC;

            init_args->loader_info.dinfo.os_linux.bios_path = tbvm_malloc(wd_len + 64);
            strcpy((char *) init_args->loader_info.dinfo.os_linux.bios_path, wd);
            strcat((char *) init_args->loader_info.dinfo.os_linux.bios_path, "/demo/binaries/bbl32.bin");

            init_args->loader_info.dinfo.os_linux.kernel_path = tbvm_malloc(wd_len + 64);
            strcpy((char *) init_args->loader_info.dinfo.os_linux.kernel_path, wd);
            strcat((char *) init_args->loader_info.dinfo.os_linux.kernel_path, "/demo/binaries/kernel-riscv32-custom.bin");

            init_args->loader_info.dinfo.os_linux.disk_image_path = tbvm_malloc(wd_len + 64);
            strcpy((char *) init_args->loader_info.dinfo.os_linux.disk_image_path, wd);
            strcat((char *) init_args->loader_info.dinfo.os_linux.disk_image_path, "/demo/binaries/rootfs.ext2");

            init_args->loader_info.dinfo.os_linux.cmdline = strdup("console=hvc0 root=/dev/vda rw");
            init_args->loader_info.dinfo.os_linux.fs_mount_tag = strdup("/dev/root");
            init_args->loader_info.dinfo.os_linux.fs_host_directory = strdup("/tmp");

            init_args->config_path = "/home/pundev/git/TinyEMU/demo/profiles/default.prd";
            free((void *) wd);
            return;
        }

        fprintf(stderr, "Error while loading default init arguments...");
    }
}

tbvm_context_t tbvm_init(const tbvm_init_t *init_args, int *err)
{
    VirtMachine *s = 0;
    int i;
    int result_code;

    BlockDeviceModeEnum drive_mode = BF_MODE_SNAPSHOT;
    VirtMachineParams p_s, *p = &p_s;

    RETURN_ERROR(0 != init_args, TBVM_INVALID_INIT_ARGS);

#ifdef DEBUG_BUILD
    fprintf(stdout, "******************************************************\n");

    if (LOADER_TYPE_DYNAMIC == init_args->loader_type) {
        if (OS_TYPE_LINUX == init_args->os_type) {
            drive_mode = BF_MODE_SNAPSHOT;

            fprintf(stdout, "Operating System: Linux\n");
            fprintf(stdout, "Memory Size: %d\n", init_args->memory_size);

            fprintf(stdout, "Loader Type: Dynamic\n");
            fprintf(stdout, "Bios Path: %s\n\n", init_args->loader_info.dinfo.os_linux.bios_path);

            fprintf(stdout, "Kernel Path: %s\n", init_args->loader_info.dinfo.os_linux.kernel_path);
            fprintf(stdout, "Kernel Command Line: %s\n\n", init_args->loader_info.dinfo.os_linux.cmdline);

            fprintf(stdout, "Disk Image Path: %s\n", init_args->loader_info.dinfo.os_linux.disk_image_path);
            fprintf(stdout, "File Mount Tag: %s\n", init_args->loader_info.dinfo.os_linux.fs_mount_tag);
            fprintf(stdout, "File System Host Directory: %s\n", init_args->loader_info.dinfo.os_linux.fs_host_directory);
        } else {
            fprintf(stdout, "Baremetal is not supported at the moment.\n");
            RETURN_ERROR(0, TBVM_INVALID_INIT_ARGS);
        }
    }  else {
        if (OS_TYPE_LINUX == init_args->os_type) {
            fprintf(stdout, "Using static image for the bootloader, kernel and rootfs...\n");
            drive_mode = BF_MODE_SNAPSHOT;
        } else {
            fprintf(stdout, "Static loader for baremetal is not supported at the moment.\n");
            RETURN_ERROR(0, TBVM_INVALID_INIT_ARGS);
        }
    }

    fprintf(stdout, "******************************************************\n");
#endif

    printf("0--------------- BUF: %p | LEN: %d\n", p->files[0].buf, p->files[0].len);
    printf("1--------------- BUF: %p | LEN: %d\n", p->files[1].buf, p->files[1].len);
    printf("2--------------- BUF: %p | LEN: %d\n", p->files[2].buf, p->files[2].len);

    virt_machine_set_defaults(p);
    virt_machine_set_config(p, init_args);

    printf("+0--------------- BUF: %p | LEN: %d\n", p->files[0].buf, p->files[0].len);
    printf("+1--------------- BUF: %p | LEN: %d\n", p->files[1].buf, p->files[1].len);
    printf("+2--------------- BUF: %p | LEN: %d\n", p->files[2].buf, p->files[2].len);


    /* override some config parameters */

    if (init_args->memory_size > 0) {
        p->ram_size = (uint64_t)init_args->memory_size << 20;
    }

    /* open the files & devices */
    for(i = 0; i < p->drive_count; i++) {
        BlockDevice *drive = block_device_init(p->tab_drive[i].filename, drive_mode);
        p->tab_drive[i].block_dev = drive;
    }

    for(i = 0; i < p->fs_count; i++) {
        FSDevice *fs;
        const char *path = p->tab_fs[i].filename;

        fs = fs_disk_init(path);
        if (!fs) {
            /* fprintf(stderr, "%s: must be a directory\n", fname); */
            RETURN_ERROR(fs != 0, TBVM_DISK_INIT_ERROR);
        }

        p->tab_fs[i].fs_dev = fs;
    }

#ifndef DISABLE_CONSOLE
    p->console = console_init(init_args->allow_ctrlc);
#endif

    p->rtc_real_time = TRUE;

    s = virt_machine_init(p);
    virt_machine_free_config(p);

    RETURN_ERROR(0 != s, TBVM_MACHINE_INIT_ERROR);
    result_code = TBVM_SUCCESS;

on_exit:
    if (err) {
        *err = result_code;
    }

    return (tbvm_context_t) s;
}

void tbvm_event_loop(tbvm_context_t ctx)
{
    if (ctx) {
        virt_machine_run((VirtMachine *) ctx);
    }
}

void tbvm_run(tbvm_context_t ctx, volatile int *state, int msec_delay)
{
    /* If state is not NULL, then set its initial value to running. */
    if (state) {
        *state = TBVM_STATE_RUN; /* running */
    }

    if (ctx) {
        for (;;) {
            tbvm_event_loop(ctx);
            if (msec_delay) tbvm_sleep(msec_delay);

            if (state && TBVM_STATE_STOP == *state) {
                fprintf(stdout, "Terminating TBVM...");
                return;
            }
        }
    }
}

void tbvm_uninit(tbvm_context_t ctx)
{
    virt_machine_end((VirtMachine *) ctx);
}

const char *tbvm_get_version_info()
{
    if (version_info[0] == 0) {
        sprintf(version_info, "%u.%u.%u", TBVM_MAJOR_VERSION, TBVM_MINOR_VERSION, TBVM_PATCH_VERSION);
    }

    return version_info;
}

const char *tbvm_get_build_info()
{
    return __TIME__ " " __DATE__;
}
