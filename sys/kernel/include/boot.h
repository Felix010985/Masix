/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef KERNEL_BOOT_H
#define KERNEL_BOOT_H

#include <stdint.h>

/* Сколько данных ядро принимает от загрузчика.
 * Ограничения стоят потому, что эти данные лежат в статике ядра. */
#define BOOT_MODULE_MAX 4
#define BOOT_MEMMAP_MAX 64
#define BOOT_CMDLINE_MAX 512

/* Поддерживаемые протоколы загрузки. Имена совпадают с именами протоколов
 * в конфигурации Limine (protocol: linux, protocol: limine, protocol:
 * multiboot2 и так далее), своего стандарта здесь не изобретается.
 * Флаг нужен только для диагностики, на работу ядра он не влияет. */
enum boot_protocol {
    BOOT_PROTOCOL_NONE = 0,
    BOOT_PROTOCOL_LINUX,
    BOOT_PROTOCOL_LIMINE,
    BOOT_PROTOCOL_MULTIBOOT2,
    BOOT_PROTOCOL_MULTIBOOT1
};

/* Прошивка машины. От неё зависит, откуда ядро берёт фреймбуфер:
 * VBE через BIOS или GOP через EFI. Загрузчик фреймбуфер не выдаёт. */
enum boot_firmware {
    BOOT_FIRMWARE_BIOS = 0,
    BOOT_FIRMWARE_EFI
};

/* Типы участков карты памяти. Нумерация как у e820 в LINUX,
 * чтобы внутри ядра не было третьего стандарта. */
#define BOOT_MEMMAP_USABLE       1
#define BOOT_MEMMAP_RESERVED     2
#define BOOT_MEMMAP_ACPI_RECLAIM 3
#define BOOT_MEMMAP_ACPI_NVS     4
#define BOOT_MEMMAP_BAD          5

/* Режим линейного фреймбуфера.
 * Заполняется без участия загрузчика: ядро само запрашивает режим
 * у VBE (BIOS) или у GOP (EFI). Поле valid поднимается, когда режим получен. */
struct framebuffer_info {
    uintptr_t address;
    uint32_t pitch;
    uint32_t width;
    uint32_t height;
    uint8_t bpp;
    uint8_t red_position;
    uint8_t red_mask_size;
    uint8_t green_position;
    uint8_t green_mask_size;
    uint8_t blue_position;
    uint8_t blue_mask_size;
    uint8_t valid;
};

/* Модуль (initramfs), загруженный загрузчиком. */
struct boot_module {
    uintptr_t start;
    uintptr_t end;
    char cmdline[64];
};

/* Участок карты памяти. */
struct boot_memmap_entry {
    uint64_t base;
    uint64_t length;
    uint32_t type;
};

/* Всё, что ядро знает о машине на старте.
 * Единственная структура, через которую ядро общается с загрузчиком. */
struct boot_info {
    enum boot_protocol protocol;
    enum boot_firmware firmware;

    /* Размеры памяти в килобайтах, как их понимает pmm. */
    uint32_t mem_lower;
    uint32_t mem_upper;

    struct framebuffer_info framebuffer;

    struct boot_module modules[BOOT_MODULE_MAX];
    uint32_t module_count;

    struct boot_memmap_entry mmap[BOOT_MEMMAP_MAX];
    uint32_t mmap_count;

    char cmdline[BOOT_CMDLINE_MAX];

    /* Физический адрес таблицы EFI System Table, 0 если EFI нет. */
    uint64_t efi_system_table;
    /* 1, если загрузчик ещё не вызвал ExitBootServices. */
    uint8_t efi_boot_services;
    /* 1, если загрузчик сам отдал линейный фреймбуфер (только запасной путь). */
    uint8_t bootloader_framebuffer;
};

/* Единственный экземпляр на всё ядро. */
extern struct boot_info boot_info;

/* Пересчитать mem_lower/mem_upper по заполненной карте памяти. */
void boot_memmap_summary(void);

#endif /* KERNEL_BOOT_H */
