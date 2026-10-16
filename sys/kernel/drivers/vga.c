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
#include <drivers/vga.h>


uint16_t cursor_x = 0;
uint16_t cursor_y = 0;
volatile uint16_t* vga_buffer = (volatile uint16_t*)0xB8000;


void vga_update_cursor(void) {
    uint16_t pos = cursor_y * VGA_WIDTH + cursor_x;
    outb(0x3D4, 0x0F);
    outb(0x3D5, (uint8_t)(pos & 0xFF));
    outb(0x3D4, 0x0E);
    outb(0x3D5, (uint8_t)((pos >> 8) & 0xFF));
}

void vga_scroll(uint8_t color) {
    for (int y = 1; y < VGA_HEIGHT; y++) {
        for (int x = 0; x < VGA_WIDTH; x++) {
            vga_buffer[(y-1) * VGA_WIDTH + x] = vga_buffer[y * VGA_WIDTH + x];
        }
    }
    uint16_t blank = (color << 8) | ' ';
    for (int x = 0; x < VGA_WIDTH; x++) {
        vga_buffer[(VGA_HEIGHT-1) * VGA_WIDTH + x] = blank;
    }
    cursor_y = VGA_HEIGHT - 1;
}

void vga_clear(uint8_t color) {
    uint16_t blank = (color << 8) | ' ';
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        vga_buffer[i] = blank;
    }
    cursor_x = 0;
    cursor_y = 0;
    vga_update_cursor();
}

void vga_raw_putc(char c, uint8_t color) {
    vga_buffer[cursor_y * VGA_WIDTH + cursor_x] = (color << 8) | c;
    cursor_x++;

    if (cursor_x >= VGA_WIDTH) {
        cursor_x = 0;
        cursor_y++;
    }
    if (cursor_y >= VGA_HEIGHT) {
        vga_scroll(color);
    }
    vga_update_cursor();
}

void vga_putc(char c) {
    vga_raw_putc(c, 0x0F);
}
