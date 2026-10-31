/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#pragma once
#include <stddef.h>
#define HEAP_SIZE 0x100000
void* alloc(size_t size);
void alloc_free(void* ptr);
void* malloc(size_t size);
void free(void* ptr);
