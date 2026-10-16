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
#include <panic.h>
#include <config.h>
#include <drivers/vga.h>

// struct exception_registers {
//     uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
//     uint32_t int_no, error_code;
//     uint32_t eip, cs, eflags;
// };

static void panic_print_bsod_str(const char* s) {
    uint8_t bsod_color = 0x1F;
    while (*s) {
        if (*s == '\n') {
            cursor_x = 0;
            cursor_y++;
            if (cursor_y >= VGA_HEIGHT) vga_scroll(bsod_color);
            vga_update_cursor();
            s++;
            continue;
        }
        vga_raw_putc(*s++, bsod_color);
    }
}

void panic_bsod(const char* msg) {
    uint8_t bsod_color = 0x1F;
    vga_clear(bsod_color);

    panic_print_bsod_str("A problem has been detected and Masix has been shut down to prevent damage\n");
    panic_print_bsod_str("to your computer.\n\n");
    panic_print_bsod_str("KERNEL_PANIC\n\n");
    panic_print_bsod_str("If this is the first time you've seen this Stop error screen,\n");
    panic_print_bsod_str("restart your computer. If this screen appears again, follow\n");
    panic_print_bsod_str("these steps:\n\n");
    panic_print_bsod_str("Check to make sure any new hardware is properly\n");
    panic_print_bsod_str("installed. If this is a new installation, ask your hardware manufacturer\n");
    panic_print_bsod_str("for any Masix updates you might need.\n\n");
    panic_print_bsod_str("If problems continue, disable or remove any newly installed hardware\n");
    panic_print_bsod_str("or software. Disable BIOS memory options such as caching or shadowing.\n\n");
    panic_print_bsod_str("Technical Information:\n\n");
    panic_print_bsod_str("*** STOP: 0x0000007B\n\n");
    panic_print_bsod_str("*** kernel.org - Address 0xB00B1E55 base at Masix Core\n\n");

    panic_print_bsod_str("Exception info: ");
    panic_print_bsod_str(msg);
    panic_print_bsod_str("\n");
}

static const char* exception_names[] = {
    "Division By Zero", "Debug", "Non Maskable Interrupt", "Breakpoint",
    "Into Detected Overflow", "Out of Bounds", "Invalid Opcode", "No Coprocessor",
    "Double Fault", "Coprocessor Segment Overrun", "Bad TSS", "Segment Not Present",
    "Stack Fault", "General Protection Fault", "Page Fault", "Unknown Interrupt"
};

static void panic_put_hex(uint32_t val) {
    char hex_chars[] = "0123456789ABCDEF";
    char buf[11];
    buf[0] = '0'; buf[1] = 'x';
    for (int i = 0; i < 8; i++) {
        buf[9 - i] = hex_chars[(val >> (i * 4)) & 0x0F];
    }
    buf[10] = '\0';
    debug_puts(buf);
}

void panic_exception_handler(struct exception_registers* regs) {
    debug_puts("Masix: Panic: Calling panic_print_bsod_str(msg);\n");

    if (regs->int_no < 16) {
        panic_bsod(exception_names[regs->int_no]);
    } else {
        panic_bsod("Unknown Exception");
    }
    debug_puts("Masix: Panic: Probably called by now, if not in LFB mode\n");

    debug_puts("Masix: Panic: Maybe called, disabling interrupts...\n");
    __asm__ __volatile__("cli");

    debug_puts("\n!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");
    debug_puts("!!!               KERNEL PANIC               !!!\n");

    if (regs->int_no < 16) {
        debug_puts("Exception: "); debug_puts(exception_names[regs->int_no]); debug_puts("\n");
    } else {
        debug_puts("Exception: Unknown\n");
    }

    debug_puts("------------------------------------------------\n");
    debug_puts("EIP:    "); panic_put_hex(regs->eip);        debug_puts("   ERROR CODE: "); panic_put_hex(regs->error_code); debug_puts("\n");
    debug_puts("EAX:    "); panic_put_hex(regs->eax);        debug_puts("   EBX:        "); panic_put_hex(regs->ebx);        debug_puts("\n");
    debug_puts("ECX:    "); panic_put_hex(regs->ecx);        debug_puts("   EDX:        "); panic_put_hex(regs->edx);        debug_puts("\n");
    debug_puts("EDI:    "); panic_put_hex(regs->edi);        debug_puts("   ESI:        "); panic_put_hex(regs->esi);        debug_puts("\n");
    debug_puts("EBP:    "); panic_put_hex(regs->ebp);        debug_puts("   ESP:        "); panic_put_hex(regs->esp);        debug_puts("\n");
    uint32_t cr2;
    __asm__ __volatile__("mov %%cr2, %0" : "=r"(cr2));
    debug_puts("CR2:    "); panic_put_hex(cr2); debug_puts("\n");
    debug_puts("CS:     "); panic_put_hex(regs->cs);         debug_puts("   EFLAGS:     "); panic_put_hex(regs->eflags);     debug_puts("\n");
    debug_puts("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!\n");

    for (;;) { __asm__ __volatile__("hlt"); }
}

void panic(const char* msg) {
    __asm__ __volatile__("cli");
    debug_puts("\n!!! KERNEL PANIC !!!\n");
    debug_puts(msg);
    debug_puts("\nSYSTEM HALTED\n");
    for (;;) { __asm__ __volatile__("hlt"); }
}

__attribute__((naked)) void panic_exception_common_stub(void) {
    __asm__ __volatile__ (
        "pusha \n\t"
        "mov $0x10, %ax \n\t"
        "mov %ax, %ds \n\t"
        "mov %ax, %es \n\t"

        "push %esp \n\t"
        "call panic_exception_handler \n\t"
    );
}

__attribute__((naked)) void panic_exception_gpf(void) {
    __asm__ __volatile__ (
        "pushl $13 \n\t"
        "jmp panic_exception_common_stub \n\t"
    );
}

__attribute__((naked)) void panic_exception_div_zero(void) {
    __asm__ __volatile__ (
        "pushl $0 \n\t"
        "pushl $0 \n\t"
        "jmp panic_exception_common_stub \n\t"
    );
}

// void __stack_chk_fail(void) {
//     while(1) {
//         __asm__ __volatile__("cli; hlt");
//     }
// }

__attribute__((naked)) void panic_exception_pf(void) {
    __asm__ __volatile__ (
        "pushl $14 \n\t"
        "jmp panic_exception_common_stub \n\t"
    );
}
