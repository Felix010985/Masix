/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix LinuxKPI
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#include <linux/printk.h>
#include <stdarg.h>

extern void vprintf(const char* fmt, ...);

int printk(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    /* linux пропускает уровень приоритета <N> */
    if (fmt[0] == '<' && fmt[1] >= '0' && fmt[1] <= '7' && fmt[2] == '>') fmt += 3;
    vprintf(fmt, args);
    va_end(args);
    return 0;
}
