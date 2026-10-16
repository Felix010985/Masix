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

#include <boot.h>
#include <config.h>
#include <realmode.h>

/* Запрос линейного фреймбуфера у VBE (VESA BIOS Extensions) через BIOS.
 * Файл специфичен для i686: из 64-битного режима реальный режим недостижим,
 * поэтому на x86-64 этот путь заменяется на GOP. */

/* Буферы BIOS в нижней памяти, из той же рабочей страницы, что и станок. */
#define VBE_INFO_PHYS 0x2200    /* сведения о контроллере, 512 байт */
#define VBE_MODE_PHYS 0x2400    /* сведения о режиме, 256 байт */

/* Смещения по спецификации VBE 2.0. */
#define VBE_INFO_SIGNATURE 0
#define VBE_INFO_VERSION   4
#define VBE_INFO_MODES_OFF 14    /* дальний указатель на список режимов */
#define VBE_INFO_MODES_SEG 16
#define VBE_INFO_MEMORY    18    /* видеопамять в блоках по 64 КБ */

#define VBE_MODE_ATTRIBUTES 0
#define VBE_MODE_PITCH      16
#define VBE_MODE_W          18
#define VBE_MODE_H          20
#define VBE_MODE_BPP        25
#define VBE_MODE_MODEL      27
#define VBE_MODE_RED_SIZE   31
#define VBE_MODE_RED_POS    32
#define VBE_MODE_GREEN_SIZE 33
#define VBE_MODE_GREEN_POS  34
#define VBE_MODE_BLUE_SIZE  35
#define VBE_MODE_BLUE_POS   36
#define VBE_MODE_LFB_BASE   40
#define VBE_MODE_LIN_PITCH      50
#define VBE_MODE_LIN_RED_SIZE   54
#define VBE_MODE_LIN_RED_POS    55
#define VBE_MODE_LIN_GREEN_SIZE 56
#define VBE_MODE_LIN_GREEN_POS  57
#define VBE_MODE_LIN_BLUE_SIZE  58
#define VBE_MODE_LIN_BLUE_POS   59

/* Бит 1 ModeAttributes: режим поддерживается оборудованием. */
#define VBE_ATTR_SUPPORTED 0x0001
/* Бит 7 ModeAttributes: режим умеет линейный фреймбуфер. */
#define VBE_ATTR_LFB 0x0080

#define VBE_MEMORY_MODEL_PACKED 4
#define VBE_MEMORY_MODEL_DIRECT 6

#define VBE_FUNC_INFO     0x4F00
#define VBE_FUNC_MODE_INFO 0x4F01
#define VBE_FUNC_SET_MODE 0x4F02

/* Флаг линейного фреймбуфера в номере режима. */
#define VBE_MODE_LFB_FLAG 0x4000

/* BIOS отвечает этим значением в AX при успехе. */
#define VBE_OK 0x004F

static uint8_t vbe_read8(uint32_t phys, uint32_t off) {
    return *(volatile uint8_t*)(uintptr_t)(phys + off);
}

static uint16_t vbe_read16(uint32_t phys, uint32_t off) {
    return *(volatile uint16_t*)(uintptr_t)(phys + off);
}

/* INT 10h с заданными регистрами. */
static int vbe_bios_call(struct realmode_regs* regs) {
    realmode_int10(regs);
    return (regs->ax == VBE_OK) ? 0 : -1;
}

/* Запросить у BIOS сведения о контроллере. */
static int vbe_controller_info(void) {
    struct realmode_regs regs;
    memset(&regs, 0, sizeof(regs));

    /* BIOS ждет в буфере подпись "VBE2", иначе отдаст сведения только VBE 1.x. */
    *(volatile uint32_t*)(uintptr_t)VBE_INFO_PHYS = 0x32454256;

    regs.ax = VBE_FUNC_INFO;
    regs.es = 0;
    regs.di = (uint16_t)VBE_INFO_PHYS;

    return vbe_bios_call(&regs);
}

/* Запросить у BIOS сведения о режиме. */
static int vbe_mode_info(uint16_t mode) {
    struct realmode_regs regs;
    memset(&regs, 0, sizeof(regs));

    regs.ax = VBE_FUNC_MODE_INFO;
    regs.cx = mode;
    regs.es = 0;
    regs.di = (uint16_t)VBE_MODE_PHYS;

    return vbe_bios_call(&regs);
}

/* Включить режим с линейным фреймбуфером. */
static int vbe_set_mode(uint16_t mode) {
    struct realmode_regs regs;
    memset(&regs, 0, sizeof(regs));

    regs.ax = VBE_FUNC_SET_MODE;
    regs.bx = (uint16_t)(mode | VBE_MODE_LFB_FLAG);

    return vbe_bios_call(&regs);
}

/* Физический адрес списка режимов. BIOS отдает дальний указатель,
 * линейный адрес считается как сегмент * 16 плюс смещение. */
static uint32_t vbe_mode_list_address(void) {
    uint16_t off = vbe_read16(VBE_INFO_PHYS, VBE_INFO_MODES_OFF);
    uint16_t seg = vbe_read16(VBE_INFO_PHYS, VBE_INFO_MODES_SEG);

    if (off == 0 && seg == 0) return 0;
    return ((uint32_t)seg << 4) + off;
}

int vbe_request(struct framebuffer_info* out) {
    memset(out, 0, sizeof(*out));

    if (vbe_controller_info() != 0) {
        debug_puts("Masix: VBE is not available\n");
        return -1;
    }

    if (vbe_read8(VBE_INFO_PHYS, 0) != 'V' || vbe_read8(VBE_INFO_PHYS, 1) != 'E' ||
        vbe_read8(VBE_INFO_PHYS, 2) != 'S' || vbe_read8(VBE_INFO_PHYS, 3) != 'A') {
        debug_puts("Masix: VESA signature not found\n");
        return -1;
    }

    uint32_t list = vbe_mode_list_address();
    if (list == 0) {
        debug_puts("Masix: VBE mode list is missing\n");
        return -1;
    }

    /* Перебираем список режимов и запоминаем подходящий.
     * Приоритет отдаем большему разрешению при 32 битах на пиксель. */
    uint16_t best_mode = 0;
    uint32_t best_area = 0;
    uint8_t best_bpp = 0;
    uint32_t best_pitch = 0;
    uint32_t best_base = 0;
    uint32_t best_width = 0;
    uint32_t best_height = 0;
    uint8_t best_red_size = 0, best_red_pos = 0;
    uint8_t best_green_size = 0, best_green_pos = 0;
    uint8_t best_blue_size = 0, best_blue_pos = 0;

    for (uint32_t i = 0; i < 256; i++) {
        uint16_t mode = *(volatile uint16_t*)(uintptr_t)(list + i * 2);
        if (mode == 0xFFFF) break;

        /* Биты 0..8 номера режима это служебные флаги, игнорируем их. */
        if (mode & 0x0400) continue;

        if (vbe_mode_info(mode) != 0) continue;

        uint16_t attributes = vbe_read16(VBE_MODE_PHYS, VBE_MODE_ATTRIBUTES);
        if ((attributes & VBE_ATTR_SUPPORTED) == 0) continue;
        if ((attributes & VBE_ATTR_LFB) == 0) continue;

        uint8_t model = vbe_read8(VBE_MODE_PHYS, VBE_MODE_MODEL);
        if (model != VBE_MEMORY_MODEL_PACKED && model != VBE_MEMORY_MODEL_DIRECT) continue;

        uint8_t bpp = vbe_read8(VBE_MODE_PHYS, VBE_MODE_BPP);
        if (bpp != 16 && bpp != 24 && bpp != 32) continue;

        uint32_t width = vbe_read16(VBE_MODE_PHYS, VBE_MODE_W);
        uint32_t height = vbe_read16(VBE_MODE_PHYS, VBE_MODE_H);
        if (width < 640 || height < 480) continue;

        uint32_t area = width * height;

        /* 32 бита лучше 24, они лучше 16; при равной глубине больше разрешение. */
        if (bpp < best_bpp) continue;
        if (bpp == best_bpp && area <= best_area) continue;

        uint16_t pitch = vbe_read16(VBE_MODE_PHYS, VBE_MODE_PITCH);
        uint16_t lin_pitch = vbe_read16(VBE_MODE_PHYS, VBE_MODE_LIN_PITCH);
        if (lin_pitch != 0) pitch = lin_pitch;

        uint32_t base = *(volatile uint32_t*)(uintptr_t)(VBE_MODE_PHYS + VBE_MODE_LFB_BASE);
        if (base == 0) continue;

        best_mode = mode;
        best_area = area;
        best_bpp = bpp;
        best_pitch = pitch;
        best_base = base;
        best_width = width;
        best_height = height;

        /* Маски цвета: у VBE 3.0 есть отдельные поля для линейного режима. */
        best_red_size = vbe_read8(VBE_MODE_PHYS, VBE_MODE_LIN_RED_SIZE);
        best_red_pos = vbe_read8(VBE_MODE_PHYS, VBE_MODE_LIN_RED_POS);
        best_green_size = vbe_read8(VBE_MODE_PHYS, VBE_MODE_LIN_GREEN_SIZE);
        best_green_pos = vbe_read8(VBE_MODE_PHYS, VBE_MODE_LIN_GREEN_POS);
        best_blue_size = vbe_read8(VBE_MODE_PHYS, VBE_MODE_LIN_BLUE_SIZE);
        best_blue_pos = vbe_read8(VBE_MODE_PHYS, VBE_MODE_LIN_BLUE_POS);

        if (best_red_size == 0 && best_green_size == 0 && best_blue_size == 0) {
            best_red_size = vbe_read8(VBE_MODE_PHYS, VBE_MODE_RED_SIZE);
            best_red_pos = vbe_read8(VBE_MODE_PHYS, VBE_MODE_RED_POS);
            best_green_size = vbe_read8(VBE_MODE_PHYS, VBE_MODE_GREEN_SIZE);
            best_green_pos = vbe_read8(VBE_MODE_PHYS, VBE_MODE_GREEN_POS);
            best_blue_size = vbe_read8(VBE_MODE_PHYS, VBE_MODE_BLUE_SIZE);
            best_blue_pos = vbe_read8(VBE_MODE_PHYS, VBE_MODE_BLUE_POS);
        }

        debug_puts("Masix: VBE mode candidate found\n");
    }

    if (best_mode == 0) {
        debug_puts("Masix: no usable VBE mode found\n");
        return -1;
    }

    if (vbe_set_mode(best_mode) != 0) {
        debug_puts("Masix: VBE mode switch failed\n");
        return -1;
    }

    /* После переключения перечитываем сведения: BIOS мог скорректировать их. */
    if (vbe_mode_info(best_mode) == 0) {
        uint16_t lin_pitch = vbe_read16(VBE_MODE_PHYS, VBE_MODE_LIN_PITCH);
        if (lin_pitch != 0) best_pitch = lin_pitch;
    }

    out->address = (uintptr_t)best_base;
    out->pitch = best_pitch;
    out->width = best_width;
    out->height = best_height;
    out->bpp = best_bpp;
    out->red_position = best_red_pos;
    out->red_mask_size = best_red_size;
    out->green_position = best_green_pos;
    out->green_mask_size = best_green_size;
    out->blue_position = best_blue_pos;
    out->blue_mask_size = best_blue_size;
    out->valid = 1;

    return 0;
}
