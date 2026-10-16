/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix LinuxKPI
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef _LINUX_SLAB_H
#define _LINUX_SLAB_H

#include <stddef.h>

typedef unsigned gfp_t;

#define GFP_KERNEL 0x01
#define GFP_ATOMIC 0x02
#define GFP_DMA    0x04

void *kmalloc(size_t size, gfp_t flags);
void *kzalloc(size_t size, gfp_t flags);
void  kfree(const void *ptr);

#endif
