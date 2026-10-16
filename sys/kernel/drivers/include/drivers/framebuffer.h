/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef DRIVERS_FRAMEBUFFER_H
#define DRIVERS_FRAMEBUFFER_H

#include <stdint.h>
#include <boot.h>

/* Драйвер линейного фреймбуфера.
 * Драйвер ничего не знает о загрузчиках: режим приходит в struct
 * framebuffer_info, а его заполняет само ядро, запросив режим у VBE
 * или у GOP. Соглашение о полях общее для всех источников режима. */

extern volatile uint8_t* framebuffer;
extern uint32_t framebuffer_pitch;
extern uint32_t framebuffer_width;
extern uint32_t framebuffer_height;
extern uint8_t  framebuffer_bpp;
/* 1 означает режим с прямыми цветами RGB. */
extern uint8_t  framebuffer_type;

/* Отобразить видеопамять и запомнить параметры режима.
 * Возвращает 0 при успехе. */
int framebuffer_init(const struct framebuffer_info* info);

int framebuffer_available(void);
uint32_t framebuffer_color_value(uint8_t color);
void framebuffer_put_pixel(uint32_t x, uint32_t y, uint32_t value);
void framebuffer_clear_color(uint32_t raw_color);

#endif /* DRIVERS_FRAMEBUFFER_H */
