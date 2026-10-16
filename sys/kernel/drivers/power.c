/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#include <stdint.h>
#include <drivers/io.h>


void power_shutdown(void) {
    outw(0x604, 0x2000);

    // print("System halted\n", VGA_RED);
    __asm__ volatile("cli");
    for (;;) {
        __asm__ volatile ("hlt");
    }
}

void power_shutdown_by_panic() {
    __asm__ volatile ("cli");
    uint64_t idt_ptr = 0;
    __asm__ volatile ("lidt (%0)" : : "r"(&idt_ptr));
    __asm__ volatile ("int3");
}

