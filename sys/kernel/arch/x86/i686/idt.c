/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#include <idt.h>

struct idt_entry idt[256];
struct idt_ptr idtp;

void idt_set_gate(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_low  = base & 0xFFFF;
    idt[num].base_high = (base >> 16) & 0xFFFF;
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

void idt_register_handler(uint8_t vector, uint32_t handler_addr, uint8_t flags) {
    idt_set_gate(vector, handler_addr, 0x08, flags);
}

__attribute__((naked)) void idt_default_interrupt_handler() {
    __asm__ __volatile__ (
        "cli\n\t"
        "hlt\n\t"
        "jmp ."
    );
}

void idt_install(void) {
    idtp.limit = (sizeof(struct idt_entry) * 256) - 1;
    idtp.base  = (uint32_t)&idt;
    uint32_t handler_addr = (uint32_t)idt_default_interrupt_handler;

    for (int i = 0; i < 256; i++) {
        idt_set_gate(i, handler_addr, 0x08, 0x8E);
    }

    __asm__ __volatile__("lidt (%0)" : : "r"(&idtp));
}
