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
#include <tty.h>
#include <drivers/framebuffer.h>
#include <drivers/vga.h>
#include <drivers/keyboard.h>
#include <fonts/default8x16.h>

extern void vga_putc_color(char c, uint8_t color) __attribute__((weak));
extern void vga_clear(uint8_t color) __attribute__((weak));

/* --- Виртуальные консоли ------------------------------------------
 *
 * Каждая консоль (tty1..tty6) держит собственный виртуальный экран -
 * сетку ячеек «символ + атрибут VGA», как в текстовом режиме Linux.
 * На физический экран содержимое попадает только у активной консоли,
 * поэтому переключение (Alt+F1..F6) сводится к перерисовке сетки.
 *
 * Ввод тоже разделен: обработчик клавиатуры отдает символ только
 * активной консоли. Поэтому getty на tty2 не перехватывает нажатия,
 * предназначенные tty1, - в отличие от общей очереди, которая была
 * тут раньше.
 *
 * "console"/"tty"/"tty0" - это текущая (активная) консоль, как в Linux,
 * а "tty1".."tty6" - фиксированные.
 */

#define TTY_COUNT       7                  /* ttys[0] не используется */
#define TTY_FIRST       1
#define TTY_LAST        (TTY_COUNT - 1)

#define TTY_MAX_COLS    256
#define TTY_MAX_ROWS    128

/* Редактирование строки: эхо, backspace и буфер. Читатель (шелл и любой
 * другой) получает готовую строку через tty_read(). */
#define TTY_LINE_BUF_SIZE 256
/* Необработанные нажатия от обработчика IRQ. */
#define TTY_RAW_SIZE      256

typedef struct {
    /* Виртуальный экран: старший байт - атрибут VGA, младший - символ. */
    uint16_t cells[TTY_MAX_ROWS * TTY_MAX_COLS];
    uint32_t cols, rows;
    uint32_t x, y;
    uint8_t  color;

    int cursor_enabled;
    int cursor_drawn;

    /* Разбор escape-последовательностей ANSI. */
    int ansi_state;
    int ansi_is_dec;
    int ansi_arg1;
    int ansi_arg2;
    int ansi_which;      /* 0 - arg1, 1 - arg2 */

    /* Нажатия, еще не разобранные читателем. Кольцо заполняет только
     * обработчик IRQ, и только для активной консоли. */
    char raw[TTY_RAW_SIZE];
    volatile uint32_t raw_head;
    volatile uint32_t raw_tail;

    /* Собираемая строка канонического режима. */
    char line_buf[TTY_LINE_BUF_SIZE];
    uint32_t line_len;
    uint32_t line_pos;

    /* Готовая строка, выдаваемая читателю по частям. */
    char deliver[TTY_LINE_BUF_SIZE];
    uint32_t deliver_len;
    uint32_t deliver_pos;
} tty_t;

static tty_t ttys[TTY_COUNT];
static int tty_current = TTY_FIRST;
static int tty_ready = 0;

static void tty_putc(tty_t *t, char c);
static void tty_backspace_t(tty_t *t);
static void tty_erase_cursor_t(tty_t *t);
static void tty_draw_cursor_t(tty_t *t);
static void tty_sync_dims(tty_t *t);

static tty_t *tty_active(void) {
    return &ttys[tty_current];
}

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

/* Подгоняем сетку консоли под текущий видеорежим. Вызывается лениво:
 * tty_init() выполняется до framebuffer_init(), поэтому размеры
 * становятся известны только к первому выводу. */
static void tty_sync_dims(tty_t *t) {
    uint32_t cols = tty_width();
    uint32_t rows = tty_height();
    if (cols > TTY_MAX_COLS) cols = TTY_MAX_COLS;
    if (rows > TTY_MAX_ROWS) rows = TTY_MAX_ROWS;
    if (cols == 0 || rows == 0) return;
    if (cols == t->cols && rows == t->rows) return;

    t->cols = cols;
    t->rows = rows;
    uint16_t blank = (uint16_t)((0x07 << 8) | ' ');
    for (uint32_t i = 0; i < cols * rows; i++) t->cells[i] = blank;
    t->x = 0;
    t->y = 0;
    t->cursor_drawn = 0;

    if (t == tty_active()) {
        if (framebuffer_available()) {
            framebuffer_clear_color(tty_get_raw_color((0x07 >> 4) & 15));
        } else if (vga_clear) {
            vga_clear(0x07);
        }
    }
}

/* --- Курсор ------------------------------------------------------ */

static void tty_xor_cursor(tty_t *t) {
    uint32_t x_start = t->x * FONT_WIDTH;
    uint32_t y_start = t->y * FONT_HEIGHT;
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

static void tty_draw_cursor_t(tty_t *t) {
    if (t != tty_active() || !framebuffer_available() || !t->cursor_enabled) return;
    if (t->cursor_drawn) return;
    tty_xor_cursor(t);
    t->cursor_drawn = 1;
}

static void tty_erase_cursor_t(tty_t *t) {
    if (t != tty_active() || !framebuffer_available() || !t->cursor_enabled) return;
    if (!t->cursor_drawn) return;
    tty_xor_cursor(t);
    t->cursor_drawn = 0;
}

/* Публичные обертки работают с текущей консолью. */
void tty_draw_cursor(void) { tty_draw_cursor_t(tty_active()); }
void tty_erase_cursor(void) { tty_erase_cursor_t(tty_active()); }

/* --- Отрисовка ячеек --------------------------------------------- */

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

/* Нарисовать одну ячейку на физическом экране - но только если эта
 * консоль активна: фоновая пишет лишь в свою сетку. */
static void tty_render_cell(tty_t *t, uint32_t col, uint32_t row) {
    if (t != tty_active()) return;
    if (col >= t->cols || row >= t->rows) return;

    uint16_t cell = t->cells[row * t->cols + col];
    char c = (char)(cell & 0xFF);
    uint8_t attr = (uint8_t)(cell >> 8);

    if (framebuffer_available()) {
        tty_draw_char_fb(col, row, c, attr);
    } else if (col < VGA_WIDTH && row < VGA_HEIGHT) {
        vga_buffer[row * VGA_WIDTH + col] = cell;
    }
}

static void tty_store_cell(tty_t *t, uint32_t col, uint32_t row, char c, uint8_t attr) {
    if (col >= t->cols || row >= t->rows) return;
    t->cells[row * t->cols + col] = (uint16_t)(((uint16_t)attr << 8) | (uint8_t)c);
    tty_render_cell(t, col, row);
}

/* Полная перерисовка: нужна после переключения консоли. */
static void tty_render_all(tty_t *t) {
    if (t != tty_active()) return;

    if (framebuffer_available()) {
        for (uint32_t row = 0; row < t->rows; row++) {
            for (uint32_t col = 0; col < t->cols; col++) {
                tty_render_cell(t, col, row);
            }
        }
    } else {
        for (uint32_t row = 0; row < t->rows && row < VGA_HEIGHT; row++) {
            for (uint32_t col = 0; col < t->cols && col < VGA_WIDTH; col++) {
                vga_buffer[row * VGA_WIDTH + col] = t->cells[row * t->cols + col];
            }
        }
        cursor_x = (uint16_t)t->x;
        cursor_y = (uint16_t)t->y;
        vga_update_cursor();
    }
    t->cursor_drawn = 0;
}

static void tty_scroll(tty_t *t) {
    uint32_t line = t->cols;

    memmove(t->cells, t->cells + line, (t->rows - 1) * line * sizeof(uint16_t));
    uint16_t blank = (uint16_t)(((uint16_t)t->color << 8) | ' ');
    for (uint32_t i = (t->rows - 1) * line; i < t->rows * line; i++) t->cells[i] = blank;

    if (t == tty_active()) {
        if (framebuffer_available()) {
            uint32_t bytes_per_line = framebuffer_pitch;
            uint32_t total_scroll_bytes = (framebuffer_height - FONT_HEIGHT) * bytes_per_line;
            memcpy((void*)framebuffer, (const void*)(framebuffer + FONT_HEIGHT * bytes_per_line), total_scroll_bytes);

            uint32_t bg_color = tty_get_raw_color((t->color >> 4) & 15);
            for (uint32_t y = framebuffer_height - FONT_HEIGHT; y < framebuffer_height; y++) {
                for (uint32_t x = 0; x < framebuffer_width; x++) {
                    framebuffer_put_pixel(x, y, bg_color);
                }
            }
        } else {
            for (uint32_t row = 0; row < t->rows && row < VGA_HEIGHT; row++) {
                for (uint32_t col = 0; col < t->cols && col < VGA_WIDTH; col++) {
                    vga_buffer[row * VGA_WIDTH + col] = t->cells[row * t->cols + col];
                }
            }
        }
    }

    t->y = t->rows - 1;
    t->cursor_drawn = 0;
}

static void tty_newline(tty_t *t) {
    t->x = 0;
    t->y++;
    if (t->y >= t->rows) tty_scroll(t);
}

/* --- Парсер ANSI ------------------------------------------------- */

static int tty_parse_ansi(tty_t *t, char c) {
    if (t->ansi_state == 0) {
        if (c != '\033') return 0;
        t->ansi_state = 1;
        t->ansi_arg1 = 0;
        t->ansi_arg2 = 0;
        t->ansi_which = 0;
        t->ansi_is_dec = 0;
        return 1;
    }
    if (t->ansi_state == 1) {
        t->ansi_state = c == '[' ? 2 : 0;
        return 1;
    }
    if (t->ansi_state == 2) {
        if (c == '?') {
            t->ansi_is_dec = 1;
            return 1;
        }
        if (c >= '0' && c <= '9') {
            int *arg = t->ansi_which ? &t->ansi_arg2 : &t->ansi_arg1;
            *arg = *arg * 10 + (c - '0');
            return 1;
        }
        if (c == ';') {
            t->ansi_which = 1;
            return 1;
        }

        /* Перед командами, которые двигают или стирают, убираем курсор,
         * иначе останется артефакт на старом месте экрана. */
        tty_erase_cursor_t(t);

        if (c == 'J' && (t->ansi_arg1 == 0 || t->ansi_arg1 == 2)) {
            uint16_t blank = (uint16_t)(((uint16_t)t->color << 8) | ' ');
            for (uint32_t i = 0; i < t->cols * t->rows; i++) t->cells[i] = blank;
            if (t == tty_active()) {
                if (framebuffer_available()) {
                    framebuffer_clear_color(tty_get_raw_color((t->color >> 4) & 15));
                } else if (vga_clear) {
                    vga_clear(t->color);
                }
            }
            t->x = 0;
            t->y = 0;
            t->cursor_drawn = 0;
        } else if (c == 'H' || c == 'f') {
            uint32_t target_row = (t->ansi_arg1 > 0) ? (uint32_t)t->ansi_arg1 - 1 : 0;
            uint32_t target_col = (t->ansi_arg2 > 0) ? (uint32_t)t->ansi_arg2 - 1 : 0;
            if (target_row < t->rows) t->y = target_row;
            if (target_col < t->cols) t->x = target_col;
        } else if (c == 'm') {
            if (t->ansi_arg1 == 0) {
                t->color = 0x07;
            } else if (t->ansi_arg1 >= 30 && t->ansi_arg1 <= 37) {
                t->color = (t->color & 0xF0) | ansi_to_vga[t->ansi_arg1 - 30];
            } else if (t->ansi_arg1 >= 90 && t->ansi_arg1 <= 97) {
                t->color = (t->color & 0xF0) | ansi_to_vga[t->ansi_arg1 - 90 + 8];
            }
            if (t->ansi_arg2 >= 30 && t->ansi_arg2 <= 37) {
                t->color = (t->color & 0xF0) | ansi_to_vga[t->ansi_arg2 - 30];
            }
        } else if (c == 'h') {
            if (t->ansi_is_dec && t->ansi_arg1 == 25) t->cursor_enabled = 1;
        } else if (c == 'l') {
            if (t->ansi_is_dec && t->ansi_arg1 == 25) t->cursor_enabled = 0;
        }
        /* Команда 'q' (мигание курсора) намеренно игнорируется: мигание
         * требует таймера, которого у консоли пока нет. */

        /* После команды возвращаем курсор на место */
        tty_draw_cursor_t(t);
        t->ansi_state = 0;
        return 1;
    }
    t->ansi_state = 0;
    return 1;
}

/* --- Backspace --------------------------------------------------- */

static void tty_backspace_t(tty_t *t) {
    if (t->x > 0) t->x--;
    else if (t->y > 0) {
        t->y--;
        t->x = t->cols - 1;
    }
    tty_store_cell(t, t->x, t->y, ' ', t->color);
}

/* --- Вывод символов ---------------------------------------------- */

static void tty_putc(tty_t *t, char c) {
    if (tty_parse_ansi(t, c)) return;

    tty_sync_dims(t);

    if (c == '\n') {
        tty_erase_cursor_t(t);
        tty_newline(t);
        tty_draw_cursor_t(t);
        return;
    }
    if (c == '\r') {
        tty_erase_cursor_t(t);
        t->x = 0;
        tty_draw_cursor_t(t);
        return;
    }
    if (c == '\t') {
        tty_erase_cursor_t(t);
        do {
            tty_store_cell(t, t->x, t->y, ' ', t->color);
            t->x++;
            if (t->x >= t->cols) tty_newline(t);
        } while (t->x && (t->x & 7));
        tty_draw_cursor_t(t);
        return;
    }
    if (c == '\b') {
        tty_erase_cursor_t(t);
        tty_backspace_t(t);
        tty_draw_cursor_t(t);
        return;
    }

    tty_erase_cursor_t(t);
    tty_store_cell(t, t->x, t->y, c, t->color);
    t->x++;
    if (t->x >= t->cols) tty_newline(t);
    tty_draw_cursor_t(t);
}

void tty_write_char(char c) {
    tty_putc(tty_active(), c);
}

void tty_backspace(void) {
    tty_erase_cursor_t(tty_active());
    tty_backspace_t(tty_active());
    tty_draw_cursor_t(tty_active());
}

/* --- Редактирование строки --------------------------------------- */

/* Здесь происходит редактирование строки: эхо, backspace, буфер.
 * Вызывается из контекста читателя (tty_read), а не из IRQ: экран
 * трогает только задача, владеющая консолью. */
static void tty_input_char(tty_t *t, char c) {
    tty_sync_dims(t);

    if (c == '\b' || c == 127) {
        /* Backspace: стираем только если в буфере строки есть символы */
        if (t->line_pos > 0) {
            t->line_pos--;
            t->line_len--;
            memmove(&t->line_buf[t->line_pos], &t->line_buf[t->line_pos + 1],
                    t->line_len - t->line_pos);
            t->line_buf[t->line_len] = '\0';
            tty_putc(t, '\b');
        }
    } else if (c == '\n' || c == '\r') {
        /* Конец строки: помечаем строку готовой и выводим перевод строки */
        t->line_buf[t->line_len++] = '\n';
        t->line_buf[t->line_len] = '\0';
        t->line_pos = t->line_len;
        tty_putc(t, '\n');
    } else {
        /* Обычный символ: добавляем в буфер и выводим */
        if (t->line_len < TTY_LINE_BUF_SIZE - 1) {
            memmove(&t->line_buf[t->line_pos + 1], &t->line_buf[t->line_pos],
                    t->line_len - t->line_pos);
            t->line_buf[t->line_pos] = c;
            t->line_len++;
            t->line_pos++;
            t->line_buf[t->line_len] = '\0';
            tty_putc(t, c);
        }
    }
}

/* --- Нажатия: кольцо сырых символов ------------------------------ */

static void tty_raw_push(tty_t *t, char c) {
    uint32_t next = (t->raw_head + 1) % TTY_RAW_SIZE;
    if (next == t->raw_tail) return;   /* переполнение: нажатие теряется */
    t->raw[t->raw_head] = c;
    t->raw_head = next;
}

static int tty_raw_pop(tty_t *t, char *c) {
    if (t->raw_tail == t->raw_head) return 0;
    *c = t->raw[t->raw_tail];
    t->raw_tail = (t->raw_tail + 1) % TTY_RAW_SIZE;
    return 1;
}

/* --- Переключение виртуальных консолей --------------------------- */

static void tty_switch_to(int n) {
    if (n < TTY_FIRST || n > TTY_LAST || n == tty_current) return;

    tty_t *old = tty_active();
    tty_t *next = &ttys[n];

    /* Убираем курсор с оставляемого экрана до подмены содержимого. */
    tty_erase_cursor_t(old);

    tty_current = n;

    tty_sync_dims(next);
    tty_render_all(next);
    tty_draw_cursor_t(next);
}

/* Приемник нажатий. Приходит из IRQ1 (клавиатура) и IRQ4 (COM1). */
static void tty_keyboard_event(uint8_t scancode, char c, uint8_t mods) {
    /* Alt+F1..F6 - переключение консолей, как в Linux. */
    if ((mods & KBD_MOD_ALT) &&
        scancode >= 0x3B && scancode <= 0x3B + (TTY_LAST - TTY_FIRST)) {
        tty_switch_to(TTY_FIRST + (int)(scancode - 0x3B));
        return;
    }

    if (!c || !tty_ready) return;
    tty_raw_push(tty_active(), c);
}

/* --- Операции консольного устройства (регистрируются в VFS) ------- */

/* Сопоставляем узел VFS конкретной консоли по имени. "console"/"tty"/
 * "tty0" - это текущая консоль, "tty1".."tty6" - фиксированные. */
static tty_t *tty_from_node(vfs_node_t *node) {
    const char *n = node ? node->name : NULL;
    if (n) {
        if (strcmp(n, "console") == 0 || strcmp(n, "tty") == 0 || strcmp(n, "tty0") == 0) {
            return tty_active();
        }
        if (n[0] == 't' && n[1] == 't' && n[2] == 'y' &&
            n[3] >= '0' + TTY_FIRST && n[3] <= '0' + TTY_LAST && n[4] == '\0') {
            return &ttys[n[3] - '0'];
        }
    }
    return tty_active();
}

int32_t tty_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)offset;
    tty_t *t = tty_from_node(node);
    if (size == 0) return 0;

    /* Строка доставлена не до конца - продолжаем с того же места. */
    if (t->deliver_pos >= t->deliver_len) {
        /* Копим строку канонического режима. Ждем только нажатия этой
         * консоли: пока она не активна, сюда ничего не придет. */
        while (1) {
            char c;
            if (!tty_raw_pop(t, &c)) {
                __asm__ volatile("hlt");
                continue;
            }
            tty_input_char(t, c);
            if (c == '\n' || c == '\r') break;
        }

        /* Забираем строку вместе с '\n' в буфер выдачи и начинаем редактор
         * заново: так канонический режим отдает данные построчно. */
        uint32_t n = t->line_len;
        if (n > TTY_LINE_BUF_SIZE) n = TTY_LINE_BUF_SIZE;
        memcpy(t->deliver, t->line_buf, n);
        t->deliver_len = n;
        t->deliver_pos = 0;

        t->line_len = 0;
        t->line_pos = 0;
        t->line_buf[0] = '\0';
    }

    uint32_t n = t->deliver_len - t->deliver_pos;
    if (n > size) n = size;
    memcpy(buffer, t->deliver + t->deliver_pos, n);
    t->deliver_pos += n;
    if (t->deliver_pos >= t->deliver_len) {
        t->deliver_len = 0;
        t->deliver_pos = 0;
    }
    return (int32_t)n;
}

int32_t tty_write(vfs_node_t *node, uint32_t offset, uint32_t size, const uint8_t *buffer) {
    (void)offset;
    tty_t *t = tty_from_node(node);
    for (uint32_t i = 0; i < size; i++) tty_putc(t, (char)buffer[i]);
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
    /* /dev/tty0 - текущая консоль, как в Linux. */
    { "tty0",    tty_read, tty_write },
    /* Шесть независимых виртуальных консолей для getty из inittab. */
    { "tty1",    tty_read, tty_write },
    { "tty2",    tty_read, tty_write },
    { "tty3",    tty_read, tty_write },
    { "tty4",    tty_read, tty_write },
    { "tty5",    tty_read, tty_write },
    { "tty6",    tty_read, tty_write }
};

void tty_init(void) {
    for (int i = 0; i < TTY_COUNT; i++) {
        tty_t *t = &ttys[i];
        t->cols = 0;
        t->rows = 0;
        t->x = 0;
        t->y = 0;
        t->color = 0x07;
        t->cursor_enabled = 1;
        t->cursor_drawn = 0;
        t->ansi_state = 0;
        t->ansi_is_dec = 0;
        t->ansi_arg1 = 0;
        t->ansi_arg2 = 0;
        t->ansi_which = 0;
        t->raw_head = 0;
        t->raw_tail = 0;
        t->line_len = 0;
        t->line_pos = 0;
        t->line_buf[0] = '\0';
        t->deliver_len = 0;
        t->deliver_pos = 0;
    }
    tty_current = TTY_FIRST;

    /* Регистрируем все устройства из tty_devices в vfs (extern функция из vfs.c)
     * TODO: разделить девайсы TTY из общей структуры VFS
     */
    for (unsigned i = 0; i < sizeof(tty_devices) / sizeof(tty_devices[0]); i++) {
        vfs_register_device(&tty_devices[i]);
    }

    /* Клавиатура отдает нажатия нам, а мы раскладываем их по консолям. */
    keyboard_set_listener(tty_keyboard_event);
    tty_ready = 1;
}
