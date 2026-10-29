/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef _MASIX_KSTAT_H
#define _MASIX_KSTAT_H

/*
 * Трюк проброса: компилятор подтянет нужную структуру kstat
 * из папки текущего таргета благодаря флагам -I в Makefile
 */
#include <asm/kstat.h>

#endif
