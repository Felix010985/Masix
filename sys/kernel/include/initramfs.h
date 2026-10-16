/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef INITRAMFS_H
#define INITRAMFS_H

#include <stdint.h>

/* Адрес и размер initramfs модуля, переданного загрузчиком */
extern uint32_t initramfs_start;
extern uint32_t initramfs_size;

/* Адрес и размер ELF-образа /bin/init внутри initramfs */
extern uint32_t initramfs_elf_start;
extern uint32_t initramfs_elf_size;

void initramfs_unpack(uint32_t archive_start, uint32_t archive_size);

#endif
