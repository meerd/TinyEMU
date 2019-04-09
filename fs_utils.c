/*
 * Misc FS utilities
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
#include <ctype.h>
#include <sys/file.h>

#include "cutils.h"
#include "list.h"
#include "fs_utils.h"

/* last byte is the version */
const uint8_t encrypted_file_magic[4] = { 0xfb, 0xa2, 0xe9, 0x01 };

char *compose_path(const char *path, const char *name)
{
    int path_len, name_len;
    char *d, *q;

    if (path[0] == '\0') {
        d = strdup(name);
    } else {
        path_len = strlen(path);
        name_len = strlen(name);
        d = malloc(path_len + 1 + name_len + 1);
        q = d;
        memcpy(q, path, path_len);
        q += path_len;
        if (path[path_len - 1] != '/')
            *q++ = '/';
        memcpy(q, name, name_len + 1);
    }
    return d;
}
