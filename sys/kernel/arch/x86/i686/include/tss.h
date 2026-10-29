/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef TSS_H
#define TSS_H

#include <stdint.h>

/* Стек ядра, используемый TSS до переключения на task_schedule */
extern uint8_t tss_kernel_stack[4096];

void tss_write(int num, uint16_t ss0, uint32_t esp0);

#endif
