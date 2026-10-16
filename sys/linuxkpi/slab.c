/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix LinuxKPI
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#include <linux/slab.h>
#include <string.h>

extern void *malloc(size_t size);
extern void  free(void* ptr);

void *kmalloc(size_t size, gfp_t flags) {
    /* kmalloc который вызывает malloc, который вызывает alloc (см. alloc.c)
     * что то здесь не чисто...
     */
    (void)flags;
    return malloc(size);
}

void *kzalloc(size_t size, gfp_t flags) {
    void *ptr = kmalloc(size, flags);
    if (ptr) memset(ptr, 0, size);
    return ptr;
}

void kfree(const void *ptr) {
    free((void *)ptr);
}
