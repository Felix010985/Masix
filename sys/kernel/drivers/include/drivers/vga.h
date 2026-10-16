/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#pragma once
#include <stdint.h>

#define VGA_WIDTH 80
#define VGA_HEIGHT 25

extern uint16_t cursor_x;
extern uint16_t cursor_y;

typedef enum {
    VGA_BLACK = 0x0,
    VGA_BLUE = 0x1,
    VGA_GREEN = 0x2,
    VGA_CYAN = 0x3,
    VGA_RED = 0x4,
    VGA_MAGENTA = 0x5,
    VGA_BROWN = 0x6,
    VGA_LIGHT_GRAY = 0x7,
    VGA_DARK_GRAY = 0x8,
    VGA_LIGHT_BLUE = 0x9,
    VGA_LIGHT_GREEN = 0xA,
    VGA_LIGHT_CYAN = 0xB,
    VGA_LIGHT_RED = 0xC,
    VGA_LIGHT_MAGENTA=0xD,
    VGA_YELLOW = 0xE,
    VGA_WHITE = 0xF
} vga_color_t;

void vga_putc_color(char c, uint8_t color);
void vga_putc(char c);
void vga_update_cursor(void);
void vga_clear(uint8_t color);
void vga_scroll(uint8_t color);
void vga_raw_putc(char c, uint8_t color);
