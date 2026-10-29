/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef _MASIX_ASM_I686_UAPI_H
#define _MASIX_ASM_I686_UAPI_H

#include <stdint.h>
#include <kstat.h>

/* Специфичные для i686 ABI системные вызовы */
int32_t sys_fstat64(int fd, struct kstat *st);
int32_t sys_getdents64(int fd, void *dirp, uint32_t count);
int32_t sys_clock_gettime32(int clk, void *ts);
int32_t sys_clock_gettime64(int clk, void *ts);
int32_t sys_getrlimit(int resource, void *rlim);
int32_t sys_mmap2(uint32_t addr, uint32_t length, uint32_t prot, uint32_t flags, int fd, uint32_t pgoffset);

#endif
