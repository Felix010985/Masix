/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#include <drivers/keyboard.h>
#include <idt.h>
#include <drivers/io.h>
#include <stdint.h>

#define KBD_DATA   0x60
#define KBD_STATUS 0x64

#define KBD_BUFFER_SIZE 256
static char kbd_buffer[KBD_BUFFER_SIZE];
volatile static int kbd_head = 0;
volatile static int kbd_tail = 0;

static int shift = 0;

static const char scancode_table[128] = {
    0, 27,'1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',
    0,'a','s','d','f','g','h','j','k','l',';','\'','`',
    0,'\\','z','x','c','v','b','n','m',',','.','/',0,
    '*',0,' ',0,
};

static const char scancode_shift_table[128] = {
    0, 27,'!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',
    0,'A','S','D','F','G','H','J','K','L',':','"','~',
    0,'|','Z','X','C','V','B','N','M','<','>','?',0,
    '*',0,' ',0,
};

void keyboard_push_char(char c) {
    int next = (kbd_head + 1) % KBD_BUFFER_SIZE;
    if (next != kbd_tail) {
        kbd_buffer[kbd_head] = c;
        kbd_head = next;
    }
}

void keyboard_handler_c(void) {
    if (inb(KBD_STATUS) & 1) {
        uint8_t sc = inb(KBD_DATA);

        if (sc & 0x80) {
            sc &= 0x7F;
            if (sc == 42 || sc == 54) shift = 0;
            return;
        }
        if (sc == 42 || sc == 54) {
            shift = 1;
            return;
        }

        char c = shift ? scancode_shift_table[sc] : scancode_table[sc];
        if (c) {
            keyboard_push_char(c);
        }
    }
}

char keyboard_getc(void) {
    while (1) {
        if (kbd_head != kbd_tail) {
            char c = kbd_buffer[kbd_tail];
            kbd_tail = (kbd_tail + 1) % KBD_BUFFER_SIZE;
            return c;
        }
        __asm__ volatile("hlt");
    }
}



__attribute__((naked)) void keyboard_handler_asm(void) {
    __asm__ __volatile__ (
        "pusha \n\t"

        "mov $0x20, %al \n\t"
        "out %al, $0x20 \n\t"

        "mov $0x10, %ax \n\t"
        "mov %ax, %ds \n\t"
        "mov %ax, %es \n\t"

        "call keyboard_handler_c \n\t"

        "popa \n\t"
        "iret"
    );
}

void keyboard_init(void) {
    idt_register_handler(0x21, (uint32_t)keyboard_handler_asm, 0xEE);
}

