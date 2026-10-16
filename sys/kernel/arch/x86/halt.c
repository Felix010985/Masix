/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#include <halt.h>
#include <config.h>

void halt(void) {
    for (;;) {
        debug_puts("Masix: Debug: This message will appear in the serial console if the CPU somehow end here. (Halt NO. 2)\n");
        __asm__ __volatile__("hlt");
    }
}
