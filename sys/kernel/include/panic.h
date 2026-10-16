/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef PANIC_H
#define PANIC_H
#pragma once

#include <stdint.h>

struct exception_registers {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t int_no, error_code;
    uint32_t eip, cs, eflags;
};
void panic_exception_handler(struct exception_registers* regs);
void panic(const char* msg);

#endif
void panic_exception_gpf(void);
void panic_exception_div_zero(void);
void panic_exception_pf(void);
