/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef KSTAT_H
#define KSTAT_H

#include <stdint.h>

struct kstat {
    uint64_t st_dev;
    int32_t  __st_dev_padding;
    int32_t  __st_ino_truncated;
    uint32_t st_mode;
    uint32_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint64_t st_rdev;
    int32_t  __st_rdev_padding;
    int64_t  st_size;
    int32_t  st_blksize;
    int64_t  st_blocks;
    int32_t  st_atime_sec;
    int32_t  st_atime_nsec;
    int32_t  st_mtime_sec;
    int32_t  st_mtime_nsec;
    int32_t  st_ctime_sec;
    int32_t  st_ctime_nsec;
    uint64_t st_ino;
} __attribute__((packed, aligned(4)));

#endif
