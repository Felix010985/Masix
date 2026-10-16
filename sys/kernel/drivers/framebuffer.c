/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#include <drivers/framebuffer.h>
#include <vmm.h>
#include <stddef.h>

volatile uint8_t* framebuffer = 0;
uint32_t framebuffer_pitch = 0;
uint32_t framebuffer_width = 0;
uint32_t framebuffer_height = 0;
uint8_t  framebuffer_bpp = 0;
uint8_t  framebuffer_type = 0;

/* Позиции и размеры полей цвета берутся из режима, который выдала прошивка. */
static uint8_t red_position, red_mask;
static uint8_t green_position, green_mask;
static uint8_t blue_position, blue_mask;

/* Перевод цвета из палитры VGA в значение пикселя текущего режима. */
uint32_t framebuffer_color_value(uint8_t color) {
    static const uint8_t palette[16][3] = {
        {0,0,0}, {0,0,170}, {0,170,0}, {0,170,170}, {170,0,0}, {170,0,170},
        {170,85,0}, {170,170,170}, {85,85,85}, {85,85,255}, {85,255,85},
        {85,255,255}, {255,85,85}, {255,85,255}, {255,255,85}, {255,255,255}
    };
    uint8_t r = palette[color & 15][0], g = palette[color & 15][1], b = palette[color & 15][2];
    if (framebuffer_bpp == 15 || framebuffer_bpp == 16)
        return ((r >> (8 - red_mask)) << red_position) | ((g >> (8 - green_mask)) << green_position) | ((b >> (8 - blue_mask)) << blue_position);
    return ((uint32_t)r << red_position) | ((uint32_t)g << green_position) | ((uint32_t)b << blue_position);
}

void framebuffer_put_pixel(uint32_t x, uint32_t y, uint32_t value) {
    if (x >= framebuffer_width || y >= framebuffer_height) return;
    volatile uint8_t* pixel = framebuffer + y * framebuffer_pitch + x * ((framebuffer_bpp + 7) / 8);
    if (framebuffer_bpp == 32) *(volatile uint32_t*)pixel = value;
    else if (framebuffer_bpp == 24) { pixel[0] = value; pixel[1] = value >> 8; pixel[2] = value >> 16; }
    else *(volatile uint16_t*)pixel = (uint16_t)value;
}

int framebuffer_init(const struct framebuffer_info* info) {
    if (!info || !info->valid) return -1;

    /* Режим без линейного фреймбуфера ядро не умеет рисовать. */
    if (info->bpp != 15 && info->bpp != 16 && info->bpp != 24 && info->bpp != 32) return -1;
    if (info->width == 0 || info->height == 0 || info->pitch == 0) return -1;

    uint32_t phys = (uint32_t)info->address;
    if (phys == 0) return -1;

    /* Общий размер видеопамяти в байтах: закрасить нужно весь экран,
     * а не только первую страницу. */
    uint32_t fb_size = info->height * info->pitch;

    /* Видеопамять отображаем в свое окно ядра: физический адрес
     * может лежать где угодно, тождественное отображение его не покроет. */
    uint32_t virt = vmm_map_mmio(phys, fb_size);
    if (virt == 0) {
        framebuffer = 0;
        return -1;
    }

    framebuffer = (volatile uint8_t*)virt;
    framebuffer_pitch = info->pitch;
    framebuffer_width = info->width;
    framebuffer_height = info->height;
    framebuffer_bpp = info->bpp;
    framebuffer_type = 1;

    red_position = info->red_position;
    red_mask = info->red_mask_size;
    green_position = info->green_position;
    green_mask = info->green_mask_size;
    blue_position = info->blue_position;
    blue_mask = info->blue_mask_size;

    return 0;
}

int framebuffer_available(void) { return framebuffer != 0 && framebuffer_type == 1; }

void framebuffer_clear_color(uint32_t raw_color) {
    if (!framebuffer_available()) return;
    for (uint32_t y = 0; y < framebuffer_height; y++) {
        for (uint32_t x = 0; x < framebuffer_width; x++) {
            framebuffer_put_pixel(x, y, raw_color);
        }
    }
}
