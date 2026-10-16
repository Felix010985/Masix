/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix LinuxKPI
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef _LINUX_ERR_H
#define _LINUX_ERR_H

#include <stdint.h>

#define MAX_ERRNO 4095

static inline int IS_ERR(const void *ptr) {
    return (uintptr_t)ptr >= (uintptr_t)-MAX_ERRNO;
}

static inline long PTR_ERR(const void *ptr) {
    return (long)(intptr_t)ptr;
}

static inline void *ERR_PTR(long error) {
    return (void *)(intptr_t)error;
}

static inline void *ERR_CAST(const void *ptr) {
    return (void *)ptr;
}

static inline int PTR_ERR_OR_ZERO(const void *ptr) {
    return IS_ERR(ptr) ? PTR_ERR(ptr) : 0;
}

#endif
