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
#include <stddef.h>
#include <string.h>

#include <gdt.h>
#include <tss.h>

struct tss_entry_struct {
    uint32_t prev_tss; uint32_t esp0; uint32_t ss0; uint32_t esp1;
    uint32_t ss1; uint32_t esp2; uint32_t ss2; uint32_t cr3;
    uint32_t eip; uint32_t eflags; uint32_t eax; uint32_t ecx;
    uint32_t edx; uint32_t ebx; uint32_t esp; uint32_t ebp;
    uint32_t esi; uint32_t edi; uint32_t es; uint32_t cs;
    uint32_t ss; uint32_t ds; uint32_t fs; uint32_t gs;
    uint32_t ldt; uint16_t trap; uint16_t iomap_base;
} __attribute__((packed));

static struct tss_entry_struct tss_entry;

uint8_t tss_kernel_stack[4096];

void tss_write(int num, uint16_t ss0, uint32_t esp0) {
    uint32_t base = (uint32_t)&tss_entry;
    uint32_t limit = sizeof(tss_entry) - 1;

    memset(&tss_entry, 0, sizeof(tss_entry));

    tss_entry.ss0 = ss0;
    tss_entry.esp0 = esp0;

    tss_entry.iomap_base = sizeof(tss_entry);

    gdt_set_entry(num, base, limit, 0x89, 0x00);
}
