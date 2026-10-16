/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef PIT_H
#define PIT_H

#include <stdint.h>

#define PIT_BASE_FREQUENCY 1193182

#define PIT_PORT_DATA0    0x40
#define PIT_PORT_COMMAND  0x43

void pit_init(uint32_t frequency);

uint32_t pit_get_ticks(void);

void pit_sleep(uint32_t ticks);

#endif
