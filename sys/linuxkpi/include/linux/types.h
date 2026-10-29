/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix LinuxKPI
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef _LINUX_TYPES_H
#define _LINUX_TYPES_H

#include <stdint.h>
#include <asm/posix_types.h>

typedef uint8_t  __u8;
typedef uint16_t __u16;
typedef uint32_t __u32;
typedef uint64_t __u64;

typedef int8_t   __s8;
typedef int16_t  __s16;
typedef int32_t  __s32;
typedef int64_t  __s64;

typedef __u16 __umode_t;
typedef __u32 __kernel_pid_t;
typedef __u32 __kernel_uid32_t;
typedef __u32 __kernel_gid32_t;

/* Экспортируем стандартные типы наружу для всего ядра Masix */
#ifndef _SIZE_T_DEFINED
#define _SIZE_T_DEFINED
typedef __kernel_size_t size_t;
#endif

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef __kernel_ssize_t ssize_t;
#endif

#endif
