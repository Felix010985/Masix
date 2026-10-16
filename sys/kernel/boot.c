/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#include <boot.h>

/* Информация о машине, полученная от загрузчика.
 * Живёт здесь, а не в загрузочном коде, чтобы ядро не зависело от протокола. */
struct boot_info boot_info;

/* Считать объем памяти по карте памяти, которую выдал загрузчик.
 * Результат идет в поля mem_lower и mem_upper (килобайты), которые
 * понимает менеджер физических страниц. */
void boot_memmap_summary(void) {
    uint64_t lower_end = 1024 * 1024;   /* 1 МБ: граница между lower и upper */
    uint64_t lower_bytes = 0;
    uint64_t upper_bytes = 0;

    for (uint32_t i = 0; i < boot_info.mmap_count; i++) {
        struct boot_memmap_entry* e = &boot_info.mmap[i];
        if (e->type != BOOT_MEMMAP_USABLE) continue;

        uint64_t start = e->base;
        uint64_t end = e->base + e->length;

        if (end <= lower_end) {
            lower_bytes += e->length;
            continue;
        }

        if (start < lower_end) {
            lower_bytes += lower_end - start;
            start = lower_end;
        }

        upper_bytes += end - start;
    }

    boot_info.mem_lower = 0;
    boot_info.mem_upper = 0;

    /* Оставляем место служебным страницам ядра в первых 16 МБ,
     * как это делает LINUX: там живут страницы DMA. */
    if (lower_bytes > 16 * 1024 * 1024) {
        boot_info.mem_lower = 16 * 1024 * 1024 / 1024;
        boot_info.mem_upper = (upper_bytes + (lower_bytes - 16ULL * 1024 * 1024)) / 1024;
    } else {
        uint64_t total = lower_bytes + upper_bytes;
        if (total > lower_end) {
            boot_info.mem_lower = 640 * 1024 * 1024 / 1024;
            boot_info.mem_upper = (total - lower_end) / 1024;
        }
    }
}
