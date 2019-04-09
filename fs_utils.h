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
#define HEAD_FILENAME "head"
#define ROOT_FILENAME "files"

#define FILEID_SIZE_MAX 32

#define FS_KEY_LEN 16

/* default block size to determine the total filesytem size */
#define FS_BLOCK_SIZE_LOG2 12
#define FS_BLOCK_SIZE (1 << FS_BLOCK_SIZE_LOG2)

typedef enum {
    FS_ERR_OK = 0,
    FS_ERR_GENERIC = -1,
    FS_ERR_SYNTAX = -2,
    FS_ERR_REVISION = -3,
    FS_ERR_FILE_ID = -4,
    FS_ERR_IO = -5,
    FS_ERR_NOENT = -6,
    FS_ERR_COUNTERS = -7,
    FS_ERR_QUOTA = -8,
    FS_ERR_PROTOCOL_VERSION = -9,
    FS_ERR_HEAD = -10,
} FSCommitErrorCode;

typedef uint64_t FSFileID;

static inline int from_hex(int c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    else if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    else if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    else
        return -1;
}

char *compose_path(const char *path, const char *name);
