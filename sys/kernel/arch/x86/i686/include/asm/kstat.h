/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef _MASIX_ASM_I686_KSTAT_H
#define _MASIX_ASM_I686_KSTAT_H

#include <stdint.h>

/* 32-битная структура метаданных файла для Linux i386 ABI (Masix i686 target) */
struct kstat {
    uint32_t st_dev;
    uint16_t __pad1;
    uint32_t st_ino;
    uint16_t st_mode;
    uint16_t st_nlink;
    uint32_t st_uid;
    uint32_t st_gid;
    uint32_t st_rdev;
    uint16_t __pad2;
    int32_t  st_size;
    int32_t  st_blksize;
    int32_t  st_blocks;
    int32_t  st_atime;
    uint32_t st_atime_nsec;
    int32_t  st_mtime;
    uint32_t st_mtime_nsec;
    int32_t  st_ctime;
    uint32_t st_ctime_nsec;
    uint32_t __unused4;
    uint32_t __unused5;
};

#endif
