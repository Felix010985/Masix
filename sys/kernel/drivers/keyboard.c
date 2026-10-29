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
#include <stddef.h>

#define KBD_DATA   0x60
#define KBD_STATUS 0x64

/* Зажатые модификаторы. */
static int shift = 0;
static int ctrl = 0;
static int alt = 0;
/* Префикс 0xE0: следующие один-два байта - расширенная клавиша. */
static int extended = 0;

static keyboard_listener_t kbd_listener = NULL;

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

void keyboard_set_listener(keyboard_listener_t listener) {
    kbd_listener = listener;
}

static void keyboard_emit(uint8_t scancode, char c, uint8_t mods) {
    if (kbd_listener) kbd_listener(scancode, c, mods);
}

void keyboard_push_char(char c) {
    /* Источник не клавиатура (COM1), поэтому модификаторов нет. */
    keyboard_emit(0, c, 0);
}

void keyboard_handler_c(void) {
    if (!(inb(KBD_STATUS) & 1)) return;

    uint8_t sc = inb(KBD_DATA);

    /* 0xE0 открывает расширенную клавишу (стрелки, правые Ctrl/Alt).
     * Символы таких клавиш нам пока не нужны, но отпускание правых
     * Ctrl/Alt важно, иначе модификатор залипнет. */
    if (sc == 0xE0) {
        extended = 1;
        return;
    }

    int is_break = (sc & 0x80) ? 1 : 0;
    uint8_t code = sc & 0x7F;

    if (extended) {
        extended = 0;
        if (code == 0x1D) ctrl = is_break ? 0 : 1;
        else if (code == 0x38) alt = is_break ? 0 : 1;
        return;
    }

    if (code == 0x2A || code == 0x36) { shift = is_break ? 0 : 1; return; }
    if (code == 0x1D) { ctrl = is_break ? 0 : 1; return; }
    if (code == 0x38) { alt = is_break ? 0 : 1; return; }
    if (is_break) return;

    char c = shift ? scancode_shift_table[code] : scancode_table[code];
    uint8_t mods = (uint8_t)((shift ? KBD_MOD_SHIFT : 0) |
                             (ctrl ? KBD_MOD_CTRL : 0) |
                             (alt ? KBD_MOD_ALT : 0));

    /* Отдаем нажатие целиком, даже если символ не получился: так
     * обрабатываются функциональные клавиши (Alt+F1..F6). */
    keyboard_emit(code, c, mods);
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
