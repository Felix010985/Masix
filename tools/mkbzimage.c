/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Toolkit
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * Сборка образа bzImage для протокола загрузки LINUX.
 *
 * Формат образа:
 *   смещение 0            сектор 0, в нем заголовок setup по смещению 0x1f1
 *   смещение 512          setup_sects секторов заголовка
 *   смещение 512*(1+N)    защищенная часть образа, грузится по 0x100000
 *
 * Загрузчики (GRUB командой linux, Limine с protocol: linux) входят
 * по 32-битной точке входа code32_start и передают в ESI нулевую страницу,
 * поэтому реальный режим не используется вовсе.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Смещения заголовка setup по протоколу x86 LINUX. */
#define SETUP_SECTS_OFF       0x1f1
#define ROOT_FLAGS_OFF        0x1f2
#define SYS_SIZE_OFF          0x1f4
#define RAM_SIZE_OFF          0x1f8
#define VID_MODE_OFF          0x1fa
#define ROOT_DEV_OFF          0x1fc
#define BOOT_FLAG_OFF         0x1fe
#define JUMP_OFF              0x200
#define HEADER_OFF            0x202
#define VERSION_OFF           0x206
#define REALMODE_SWTCH_OFF    0x208
#define START_SYS_SEG_OFF     0x20c
#define KERNEL_VERSION_OFF    0x20e
#define TYPE_OF_LOADER_OFF    0x210
#define LOADFLAGS_OFF         0x211
#define SETUP_MOVE_SIZE_OFF   0x212
#define CODE32_START_OFF      0x214
#define RAMDISK_IMAGE_OFF     0x218
#define RAMDISK_SIZE_OFF      0x21c
#define HEAP_END_PTR_OFF      0x224
#define CMD_LINE_PTR_OFF      0x228
#define INITRD_ADDR_MAX_OFF   0x22c
#define KERNEL_ALIGNMENT_OFF  0x230
#define RELOCATABLE_OFF       0x234
#define MIN_ALIGNMENT_OFF     0x235
#define XLOADFLAGS_OFF        0x236
#define CMDLINE_SIZE_OFF      0x238
#define HARDWARE_SUBARCH_OFF  0x23c
#define PAYLOAD_OFFSET_OFF    0x248
#define PAYLOAD_LENGTH_OFF    0x24c
#define SETUP_DATA_OFF        0x250
#define PREF_ADDRESS_OFF      0x258
#define INIT_SIZE_OFF         0x260
#define HANDOVER_OFFSET_OFF   0x264
#define KERNEL_INFO_OFF_OFF   0x268

/* Константы протокола. */
#define LINUX_SETUP_MAGIC 0x53726448
#define LINUX_BOOT_FLAG   0xAA55
#define LINUX_LOADED_HIGH 0x01
#define LINUX_VERSION     0x020a

/* Адрес, по которому грузится защищенная часть образа. */
#define LINUX_BZIMAGE_ADDR 0x100000

/* Количество дополнительных секторов заголовка. */
#define LINUX_SETUP_SECTS 1

static void put_u8(uint8_t* b, uint32_t off, uint8_t v) { b[off] = v; }

static void put_u16(uint8_t* b, uint32_t off, uint16_t v) {
    b[off + 0] = (uint8_t)(v & 0xFF);
    b[off + 1] = (uint8_t)((v >> 8) & 0xFF);
}

static void put_u32(uint8_t* b, uint32_t off, uint32_t v) {
    b[off + 0] = (uint8_t)(v & 0xFF);
    b[off + 1] = (uint8_t)((v >> 8) & 0xFF);
    b[off + 2] = (uint8_t)((v >> 16) & 0xFF);
    b[off + 3] = (uint8_t)((v >> 24) & 0xFF);
}

static void put_u64(uint8_t* b, uint32_t off, uint64_t v) {
    put_u32(b, off, (uint32_t)(v & 0xFFFFFFFF));
    put_u32(b, off + 4, (uint32_t)((v >> 32) & 0xFFFFFFFF));
}

int main(int argc, char** argv) {
    if (argc != 3) {
        fprintf(stderr, "использование: %s <kernel.bin> <kernel.bzImage>\n", argv[0]);
        return 1;
    }

    FILE* in = fopen(argv[1], "rb");
    if (in == NULL) {
        fprintf(stderr, "не открывается %s\n", argv[1]);
        return 1;
    }

    fseek(in, 0, SEEK_END);
    long kernel_size = ftell(in);
    fseek(in, 0, SEEK_SET);

    if (kernel_size <= 0) {
        fprintf(stderr, "пустой образ %s\n", argv[1]);
        fclose(in);
        return 1;
    }

    uint8_t* kernel = malloc((size_t)kernel_size);
    if (kernel == NULL || fread(kernel, 1, (size_t)kernel_size, in) != (size_t)kernel_size) {
        fprintf(stderr, "не читается %s\n", argv[1]);
        fclose(in);
        return 1;
    }
    fclose(in);

    /* Защищенная часть образа совпадает с образом ядра: загрузчик кладет ее
     * по адресу 0x100000, а ядро слинковано именно так, что его первый байт
     * это точка входа. Поле code32_start задано как адрес загрузки, потому
     * что GRUB считает его относительно базового адреса образа. */
    if (kernel[0] == 0 && kernel[1] == 0) {
        fprintf(stderr, "предупреждение: начало образа не похоже на код входа\n");
    }

    size_t setup_bytes = (size_t)(1 + LINUX_SETUP_SECTS) * 512;
    uint8_t* out = calloc(1, setup_bytes);
    if (out == NULL) {
        fprintf(stderr, "не хватает памяти\n");
        return 1;
    }

    put_u8(out, SETUP_SECTS_OFF, LINUX_SETUP_SECTS);
    put_u16(out, ROOT_FLAGS_OFF, 0);
    put_u32(out, SYS_SIZE_OFF, (uint32_t)((kernel_size + 15) / 16));
    put_u16(out, RAM_SIZE_OFF, 0);
    put_u16(out, VID_MODE_OFF, 0xFFFF);
    put_u16(out, ROOT_DEV_OFF, 0);
    put_u16(out, BOOT_FLAG_OFF, LINUX_BOOT_FLAG);
    /* Поле jump не используется: вход выполняется по 32-битной точке. */
    put_u16(out, JUMP_OFF, 0);
    put_u32(out, HEADER_OFF, LINUX_SETUP_MAGIC);
    put_u16(out, VERSION_OFF, LINUX_VERSION);
    put_u32(out, REALMODE_SWTCH_OFF, 0);
    put_u16(out, START_SYS_SEG_OFF, 0);
    put_u16(out, KERNEL_VERSION_OFF, 0);
    put_u8(out, TYPE_OF_LOADER_OFF, 0xFF);
    put_u8(out, LOADFLAGS_OFF, LINUX_LOADED_HIGH);
    put_u16(out, SETUP_MOVE_SIZE_OFF, 0);
    put_u32(out, CODE32_START_OFF, LINUX_BZIMAGE_ADDR);
    put_u32(out, RAMDISK_IMAGE_OFF, 0);
    put_u32(out, RAMDISK_SIZE_OFF, 0);
    put_u16(out, HEAP_END_PTR_OFF, 0);
    put_u32(out, CMD_LINE_PTR_OFF, 0);
    put_u32(out, INITRD_ADDR_MAX_OFF, 0x37FFFFFF);
    put_u32(out, KERNEL_ALIGNMENT_OFF, 0x100000);
    put_u8(out, RELOCATABLE_OFF, 0);
    put_u8(out, MIN_ALIGNMENT_OFF, 0);
    put_u16(out, XLOADFLAGS_OFF, 0);
    put_u32(out, CMDLINE_SIZE_OFF, 4096);
    put_u32(out, HARDWARE_SUBARCH_OFF, 0);
    put_u64(out, 0x240, 0);
    put_u32(out, PAYLOAD_OFFSET_OFF, 0);
    put_u32(out, PAYLOAD_LENGTH_OFF, 0);
    put_u64(out, SETUP_DATA_OFF, 0);
    put_u64(out, PREF_ADDRESS_OFF, LINUX_BZIMAGE_ADDR);
    put_u32(out, INIT_SIZE_OFF, (uint32_t)kernel_size);
    put_u32(out, HANDOVER_OFFSET_OFF, 0);
    put_u32(out, KERNEL_INFO_OFF_OFF, 0);

    FILE* file = fopen(argv[2], "wb");
    if (file == NULL) {
        fprintf(stderr, "не создается %s\n", argv[2]);
        return 1;
    }

    if (fwrite(out, 1, setup_bytes, file) != setup_bytes ||
        fwrite(kernel, 1, (size_t)kernel_size, file) != (size_t)kernel_size) {
        fprintf(stderr, "не пишется %s\n", argv[2]);
        fclose(file);
        return 1;
    }

    fclose(file);
    free(out);
    free(kernel);

    printf("bzImage: %s, защищенная часть %ld байт по 0x%X\n", argv[2], kernel_size,
           LINUX_BZIMAGE_ADDR);
    return 0;
}
