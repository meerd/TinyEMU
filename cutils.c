/*
 * Misc C utilities
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
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <stdarg.h>
#include <sys/time.h>
#include <ctype.h>

#include "cutils.h"

void *mallocz(size_t size)
{
    void *ptr;
    ptr = malloc(size);
    if (!ptr)
        return NULL;
    memset(ptr, 0, size);
    return ptr;
}

void dbuf_init(DynBuf *s)
{
    memset(s, 0, sizeof(*s));
}

void dbuf_write(DynBuf *s, size_t offset, const uint8_t *data, size_t len)
{
    size_t end, new_size;
    new_size = end = offset + len;
    if (new_size > s->allocated_size) {
        new_size = max_int(new_size, s->allocated_size * 3 / 2);
        s->buf = realloc(s->buf, new_size);
        s->allocated_size = new_size;
    }
    memcpy(s->buf + offset, data, len);
    if (end > s->size)
        s->size = end;
}

void dbuf_putc(DynBuf *s, uint8_t c)
{
    dbuf_write(s, s->size, &c, 1);
}

void dbuf_putstr(DynBuf *s, const char *str)
{
    dbuf_write(s, s->size, (const uint8_t *)str, strlen(str));
}

void dbuf_free(DynBuf *s)
{
    free(s->buf);
    memset(s, 0, sizeof(*s));
}
