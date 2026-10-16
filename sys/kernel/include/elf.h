/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef ELF_H
#define ELF_H

#include <stdint.h>

#include <vfs.h>

void* elf_load_binary(uint32_t file_start);
void* elf_load_node(vfs_node_t* node);

#endif
