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
#include <stdio.h>
#include <vfs.h>
#include <drivers/framebuffer.h>
#include <drivers/vga.h>
#include <drivers/keyboard.h>

#include <fonts/default8x16.h>

extern void vga_putc_color(char c, uint8_t color) __attribute__((weak));
extern void vga_clear(uint8_t color) __attribute__((weak));

static uint32_t tty_x = 0;
static uint32_t tty_y = 0;

static int cursor_enabled = 1;
static int cursor_blinking = 1;
static int cursor_state = 0;
static int cursor_drawn = 0;

static uint8_t tty_color = 0x07;

static int ansi_state = 0;
static int ansi_arg1 = 0;
static int ansi_arg2 = 0;
static int* ansi_current_arg = NULL;

/* --- Редактирование строки ----------------------------------------
 * Редактирование на уровне ядра: эхо, backspace, буфер строки.
 * Читатель (шелл и любой другой) получает готовую строку.
 */
#define TTY_LINE_BUF_SIZE 256
static char tty_line_buf[TTY_LINE_BUF_SIZE];
static volatile uint32_t tty_line_len = 0;
static volatile uint32_t tty_line_pos = 0;
static volatile int tty_line_complete = 0;

/* --- Таблицы цветов ANSI ----------------------------------------- */
static const uint8_t ansi_to_vga[16] = {
    0,  4,  2,  6,  1,  5,  3,  7,
    8,  12, 10, 14, 9,  13, 11, 15
};

static const uint8_t vga_palette[16][3] = {
    {0, 0, 0},       {0, 0, 170},     {0, 170, 0},     {0, 170, 170},
    {170, 0, 0},     {170, 0, 170},   {170, 85, 0},    {170, 170, 170},
    {85, 85, 85},    {85, 85, 255},   {85, 255, 85},   {85, 255, 255},
    {255, 85, 85},   {255, 85, 255},  {255, 255, 85},  {255, 255, 255}
};

static uint32_t tty_get_raw_color(uint8_t color_index) {
    uint8_t r = vga_palette[color_index & 15][0];
    uint8_t g = vga_palette[color_index & 15][1];
    uint8_t b = vga_palette[color_index & 15][2];

    if (framebuffer_bpp == 15 || framebuffer_bpp == 16) {
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    }
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

uint32_t tty_width(void) {
    return framebuffer_available() ? (framebuffer_width / FONT_WIDTH) : VGA_WIDTH;
}

uint32_t tty_height(void) {
    return framebuffer_available() ? (framebuffer_height / FONT_HEIGHT) : VGA_HEIGHT;
}

/* --- Обработка курсора ------------------------------------------- */

static void tty_xor_cursor(void) {
    uint32_t x_start = tty_x * FONT_WIDTH;
    uint32_t y_start = tty_y * FONT_HEIGHT;
    uint32_t inv_mask = framebuffer_bpp == 16 ? 0xFFFF : 0xFFFFFF;

    for (uint32_t py = 0; py < FONT_HEIGHT; py++) {
        for (uint32_t px = 0; px < FONT_WIDTH; px++) {
            uint32_t x = x_start + px;
            uint32_t y = y_start + py;
            if (x >= framebuffer_width || y >= framebuffer_height) continue;

            volatile uint8_t* pixel = framebuffer + y * framebuffer_pitch + x * ((framebuffer_bpp + 7) / 8);
            if (framebuffer_bpp == 32) {
                *(volatile uint32_t*)pixel ^= inv_mask;
            } else if (framebuffer_bpp == 24) {
                pixel[0] ^= 0xFF; pixel[1] ^= 0xFF; pixel[2] ^= 0xFF;
            } else {
                *(volatile uint16_t*)pixel ^= (uint16_t)inv_mask;
            }
        }
    }
}

void tty_draw_cursor(void) {
    if (!framebuffer_available() || !cursor_enabled) return;
    if (cursor_drawn) return;
    tty_xor_cursor();
    cursor_drawn = 1;
}

void tty_erase_cursor(void) {
    if (!framebuffer_available() || !cursor_enabled) return;
    if (!cursor_drawn) return;
    tty_xor_cursor();
    cursor_drawn = 0;
}

/* --- Отрисовка символов ------------------------------------------ */

static void tty_draw_char_fb(uint32_t col, uint32_t row, char c, uint8_t color) {
    const uint8_t* glyph = &font_data[(uint8_t)c * FONT_HEIGHT];
    uint32_t x_start = col * FONT_WIDTH;
    uint32_t y_start = row * FONT_HEIGHT;

    uint32_t foreground = tty_get_raw_color(color & 15);
    uint32_t background = tty_get_raw_color((color >> 4) & 15);

    for (uint32_t py = 0; py < FONT_HEIGHT; py++) {
        uint8_t glyph_row = glyph[py];
        for (uint32_t px = 0; px < FONT_WIDTH; px++) {
            uint32_t pixel_color = (glyph_row & (0x80 >> px)) ? foreground : background;
            framebuffer_put_pixel(x_start + px, y_start + py, pixel_color);
        }
    }
}

static void tty_scroll(void) {
    if (framebuffer_available()) {
        uint32_t font_height_pixels = FONT_HEIGHT;
        uint32_t bytes_per_line = framebuffer_pitch;
        uint32_t total_scroll_bytes = (framebuffer_height - font_height_pixels) * bytes_per_line;

        memcpy((void*)framebuffer, (const void*)(framebuffer + font_height_pixels * bytes_per_line), total_scroll_bytes);

        uint32_t bg_color = tty_get_raw_color((tty_color >> 4) & 15);
        for (uint32_t y = framebuffer_height - font_height_pixels; y < framebuffer_height; y++) {
            for (uint32_t x = 0; x < framebuffer_width; x++) {
                framebuffer_put_pixel(x, y, bg_color);
            }
        }
        tty_y = tty_height() - 1;
    }
}

static void tty_newline(void) {
    tty_x = 0;
    tty_y++;
    if (tty_y >= tty_height()) tty_scroll();
}

/* --- Парсер ANSI ------------------------------------------------- */

static int tty_parse_ansi(char c) {
    static int ansi_is_dec = 0;

    if (ansi_state == 0) {
        if (c != '\033') return 0;
        ansi_state = 1;
        ansi_arg1 = 0;
        ansi_arg2 = 0;
        ansi_current_arg = &ansi_arg1;
        ansi_is_dec = 0;
        return 1;
    }
    if (ansi_state == 1) {
        ansi_state = c == '[' ? 2 : 0;
        return 1;
    }
    if (ansi_state == 2) {
        if (c == '?') {
            ansi_is_dec = 1;
            return 1;
        }
        if (c >= '0' && c <= '9') {
            *ansi_current_arg = *ansi_current_arg * 10 + c - '0';
            return 1;
        }
        if (c == ';') {
            ansi_current_arg = &ansi_arg2;
            return 1;
        }

        /* Перед командами, которые двигают или стирают, убираем курсор,
         * иначе останется артефакт на старом месте экрана. */
        tty_erase_cursor();

        if (c == 'J' && (ansi_arg1 == 0 || ansi_arg1 == 2)) {
            if (framebuffer_available()) {
                uint32_t bg_raw = tty_get_raw_color((tty_color >> 4) & 15);
                framebuffer_clear_color(bg_raw);
            } else {
                if (vga_clear) vga_clear(tty_color);
            }
            tty_x = 0;
            tty_y = 0;
        } else if (c == 'H' || c == 'f') {
            uint32_t target_row = (ansi_arg1 > 0) ? ansi_arg1 - 1 : 0;
            uint32_t target_col = (ansi_arg2 > 0) ? ansi_arg2 - 1 : 0;
            if (target_row < tty_height()) tty_y = target_row;
            if (target_col < tty_width()) tty_x = target_col;
        } else if (c == 'm') {
            if (ansi_arg1 == 0) {
                tty_color = 0x07;
            } else if (ansi_arg1 >= 30 && ansi_arg1 <= 37) {
                tty_color = (tty_color & 0xF0) | ansi_to_vga[ansi_arg1 - 30];
            } else if (ansi_arg1 >= 90 && ansi_arg1 <= 97) {
                tty_color = (tty_color & 0xF0) | ansi_to_vga[ansi_arg1 - 90 + 8];
            }
            if (ansi_arg2 >= 30 && ansi_arg2 <= 37) {
                tty_color = (tty_color & 0xF0) | ansi_to_vga[ansi_arg2 - 30];
            }
        }
        else if (c == 'h') {
            if (ansi_is_dec && ansi_arg1 == 25) {
                cursor_enabled = 1;
            }
        }
        else if (c == 'l') {
            if (ansi_is_dec && ansi_arg1 == 25) {
                cursor_enabled = 0;
            }
        }
        else if (c == 'q') {
            if (ansi_arg1 == 1) {
                cursor_blinking = 1;
            } else if (ansi_arg1 == 2) {
                cursor_blinking = 0;
                cursor_state = 1;
            }
        }

        /* После команды возвращаем курсор на место */
        tty_draw_cursor();

        ansi_state = 0;
        return 1;
    }
    ansi_state = 0;
    return 1;
}

/* --- Backspace --------------------------------------------------- */

void tty_backspace(void) {
    if (tty_x > 0) tty_x--;
    else if (tty_y > 0) {
        tty_y--;
        tty_x = tty_width() - 1;
    }
    if (framebuffer_available()) {
        tty_draw_char_fb(tty_x, tty_y, ' ', tty_color);
    } else {
        if (vga_putc_color) vga_putc_color('\b', tty_color);
    }
}

/* --- Вывод символов ---------------------------------------------- */

void tty_write_char(char c) {
    if (tty_parse_ansi(c)) return;

    if (framebuffer_available()) {
        if (c == '\n') {
            tty_erase_cursor();
            tty_newline();
            tty_draw_cursor();
            return;
        }
        if (c == '\r') {
            tty_erase_cursor();
            tty_x = 0;
            tty_draw_cursor();
            return;
        }
        if (c == '\t') {
            tty_erase_cursor();
            do {
                tty_draw_char_fb(tty_x, tty_y, ' ', tty_color);
                tty_x++;
                if (tty_x >= tty_width()) tty_newline();
            } while (tty_x && (tty_x & 7));
            tty_draw_cursor();
            return;
        }
        if (c == '\b') {
            tty_erase_cursor();
            tty_backspace();
            tty_draw_cursor();
            return;
        }

        tty_erase_cursor();
        tty_draw_char_fb(tty_x, tty_y, c, tty_color);
        tty_x++;
        if (tty_x >= tty_width()) tty_newline();
        tty_draw_cursor();
    } else {
        if (vga_putc_color) vga_putc_color(c, tty_color);
    }
}

/* --- Редактирование строки: ввод --------------------------------- */

/* Здесь происходит редактирование строки: эхо, backspace, буфер.
 * Читатель (шелл и т.п.) получает готовую строку через tty_read().
 */
static void tty_input_char(char c) {
    if (c == '\b' || c == 127) {
        /* Backspace: стираем только если в буфере строки есть символы */
        if (tty_line_pos > 0) {
            tty_line_pos--;
            tty_line_len--;
            memmove(&tty_line_buf[tty_line_pos], &tty_line_buf[tty_line_pos + 1],
                    tty_line_len - tty_line_pos);
            tty_line_buf[tty_line_len] = '\0';
            tty_erase_cursor();
            tty_backspace();
            tty_draw_cursor();
        }
    } else if (c == '\n' || c == '\r') {
        /* Конец строки: помечаем строку готовой и выводим перевод строки */
        tty_line_buf[tty_line_len++] = '\n';
        tty_line_buf[tty_line_len] = '\0';
        tty_line_pos = tty_line_len;
        tty_line_complete = 1;
        tty_write_char('\n');
    } else {
        /* Обычный символ: добавляем в буфер и выводим */
        if (tty_line_len < TTY_LINE_BUF_SIZE - 1) {
            memmove(&tty_line_buf[tty_line_pos + 1], &tty_line_buf[tty_line_pos],
                    tty_line_len - tty_line_pos);
            tty_line_buf[tty_line_pos] = c;
            tty_line_len++;
            tty_line_pos++;
            tty_line_buf[tty_line_len] = '\0';
            tty_write_char(c);
        }
    }
}

/* --- Операции консольного устройства (регистрируются в VFS) ------- */

int32_t tty_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    (void)offset;
    if (size == 0) return 0;

    /* Передаем нажатия в редактор строки, пока строка не будет введена */
    while (1) {
        char c = keyboard_getc();
        if (c == '\r') c = '\n';
        tty_input_char(c);
        if (c == '\n') break;
    }

    /* Отдаем готовую строку вызывающему */
    uint32_t len = tty_line_len - 1; /* без символа перевода строки */
    if (len > size - 1) len = size - 1;
    memcpy(buffer, tty_line_buf, len);
    buffer[len] = '\0';

    tty_line_len = 0;
    tty_line_pos = 0;
    tty_line_complete = 0;
    tty_line_buf[0] = '\0';

    return (int32_t)len;
}

int32_t tty_write(vfs_node_t *node, uint32_t offset, uint32_t size, const uint8_t *buffer) {
    (void)node;
    (void)offset;
    for (uint32_t i = 0; i < size; i++) tty_write_char((char)buffer[i]);
    return (int32_t)size;
}

/* --- Регистрация --------------------------------------------------
 * Единственное нарушение правила названия функции:
 * vfs_device_t должно быть в vfs.c, но так как здесь регистрация только TTY то можно.
 */

static const vfs_device_t tty_devices[] = {
    /* "console" обслуживает stdio (fd 0/1/2), остальное - узлы /dev */
    { "console", tty_read, tty_write },
    { "tty",     tty_read, tty_write },
    /* /dev/tty0 и /dev/tty1: текущая консоль. Виртуальных
     * консолей в Masix пока нет, поэтому это то же самое устройство. */
    // TODO: Сделать виртуальные консоли
    { "tty0",    tty_read, tty_write },
    { "tty1",    tty_read, tty_write }
};

void tty_init(void) {
    /* Регистрируем все устройства из tty_devices в vfs (extern функция из vfs.c)
     * TODO: разделить девайсы TTY из общей структуры VFS
     */
    for (unsigned i = 0; i < sizeof(tty_devices) / sizeof(tty_devices[0]); i++) {
        vfs_register_device(&tty_devices[i]);
    }
}
