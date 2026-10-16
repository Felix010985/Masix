/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#include <pmm.h>
#include <string.h>
#include <stdbool.h>
#include <config.h>


#define PMM_MAX_PHYS        0xFFFFFFFFULL
#define PMM_PAGES_LIMIT     (PMM_MAX_PHYS / PAGE_SIZE)
#define PMM_BITMAP_WORDS    ((PMM_PAGES_LIMIT + 31) / 32)
#define PMM_MIN_PHYS        0x01000000 /* Выделяем память строго выше 16МБ, чтобы не затереть код ядра */

static uint32_t pmm_bitmap[PMM_BITMAP_WORDS];
static uint32_t g_free_pages = 0;
static uint32_t g_memory_start = 0;
static uint32_t g_memory_end = 0;
static bool g_ready = false;


static inline void pmm_page_set_allocated(uint32_t phys, bool alloc) {
    uint32_t page_num = phys / PAGE_SIZE;
    uint32_t word = page_num / 32;
    uint32_t bit = page_num % 32;
    if (alloc) pmm_bitmap[word] |= (1U << bit);
    else pmm_bitmap[word] &= ~(1U << bit);
}

static inline bool pmm_page_is_allocated(uint32_t phys) {
    uint32_t page_num = phys / PAGE_SIZE;
    uint32_t word = page_num / 32;
    uint32_t bit = page_num % 32;
    //debug_puts("Masix: Debug: pmm_page_is_allocated executed.\n");
    return (pmm_bitmap[word] >> bit) & 1U;
}

void pmm_init(uint32_t mem_lower, uint32_t mem_upper) {
    memset(pmm_bitmap, 0xFF, sizeof(pmm_bitmap)); /* Изначально размечаем всю память как занятую */

    /* mem_upper отдается в КБ сверху 1 МБ */
    g_memory_start = PMM_MIN_PHYS;
    g_memory_end = 1024 * 1024 + (mem_upper * 1024);

    if (g_memory_end <= g_memory_start) return;

    g_free_pages = (g_memory_end - g_memory_start) / PAGE_SIZE;

    /* Освобождаем страницы в доступном диапазоне */
    for (uint32_t p = g_memory_start; p < g_memory_end; p += PAGE_SIZE) {
        pmm_page_set_allocated(p, false);
    }

    g_ready = true;
    debug_puts("Masix: PMM: Physical Memory Manager initialized successfully.\n");
}

uint32_t pmm_alloc_page(void) {
    if (!g_ready || !g_free_pages) return 0;

    for (uint32_t p = g_memory_start; p < g_memory_end; p += PAGE_SIZE) {
        if (!pmm_page_is_allocated(p)) {
            pmm_page_set_allocated(p, true);
            g_free_pages--;
            return p;
        }
    }
    return 0;
}

uint32_t pmm_alloc_zeroed(void) {
    uint32_t phys = pmm_alloc_page();
    if (!phys) return 0;
    memset((void*)phys, 0, PAGE_SIZE);
    debug_puts("Masix: Debug: pmm_alloc_zeroed executed.\n");
    return phys;
}

void pmm_free_page(uint32_t phys) {
    if (!g_ready || phys < g_memory_start || phys >= g_memory_end) return;
    if (!pmm_page_is_allocated(phys)) return; /* Защита от double free */

        pmm_page_set_allocated(phys, false);
    g_free_pages++;
    debug_puts("Masix: Debug: pmm_free_page executed.\n");
}

uint32_t pmm_free_count(void) { return g_free_pages; }
