/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef PMM_H
#define PMM_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096

void pmm_init(uint32_t mem_lower, uint32_t mem_upper);
uint32_t pmm_alloc_page(void);
uint32_t pmm_alloc_zeroed(void);
void pmm_free_page(uint32_t phys);
uint32_t pmm_free_count(void);

#endif
