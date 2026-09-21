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
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <pmm.h>
#include <vmm.h>
#include <drivers/serial.h>

/* FelixProfi: Я забыл короче удалить вот эти вот дефайны, но надо было бы потому что
 * все что нужно уже находится в vmm.h, но на всякий случай пока буду держать это здесь.
#define PAGE_SIZE     4096
#define PAGE_MASK     0xFFFFF000
#define PTE_P         0x001
#define PTE_W         0x002
#define PTE_U         0x004
#define PTE_ADDR_MASK 0xFFFFF000
#define USER_TOP      0xC0000000
*/

struct vmm_space { uint32_t pd_phys; };
static struct vmm_space g_user;
static bool g_user_valid = false;

__attribute__((aligned(4096))) uint32_t kernel_page_directory[1024];

static inline uint32_t idx2(uint32_t v) { return (v >> 22) & 0x3FF; }
static inline uint32_t idx1(uint32_t v) { return (v >> 12) & 0x3FF; }
static inline bool canonical_user(uint32_t va) { return va < USER_TOP; }

uint32_t* table(uint32_t phys) {
    if (!phys || (phys & (PAGE_SIZE - 1))) return NULL;
    return (uint32_t*)phys;
}

static uint32_t* ensure_user_child(uint32_t* parent, uint32_t idx) {
    uint32_t e = parent[idx];
    if (!(e & PAGE_PRESENT)) {
        uint32_t phys = pmm_alloc_zeroed();
        if (!phys) return NULL;
        parent[idx] = phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        return table(phys);
    }
    puts_com1("Masix: Debug: ensure_user_child executed.\n");
    return table(e & PTE_ADDR_MASK);
}

int vmm_user_map(uint32_t virt, uint32_t phys, uint32_t flags) {
    if (!g_user_valid || !canonical_user(virt) || (virt & 0xFFF) || (phys & 0xFFF))
        return -1;

    uint32_t* pd = table(g_user.pd_phys);
    if (!pd) return -1;

    uint32_t* pt = ensure_user_child(pd, idx2(virt));
    if (!pt) return -1;

    pt[idx1(virt)] = (phys & PTE_ADDR_MASK) | PAGE_PRESENT | PAGE_USER | (flags & PAGE_WRITE);

    uint32_t current_cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(current_cr3));
    if ((current_cr3 & PAGE_MASK) == (g_user.pd_phys & PAGE_MASK))
        __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
    puts_com1("Masix: Debug: vmm_user_map executed.\n");

    return 0;
}

int vmm_userspace_create(void) {
    uint32_t pd_phys = pmm_alloc_zeroed();
    if (!pd_phys) return -1;

    uint32_t* dst = table(pd_phys);

    uint32_t kpt_phys = pmm_alloc_zeroed();
    if (!kpt_phys) return -1;
    uint32_t* kpt_virt = table(kpt_phys);

    for (uint32_t i = 0; i < 1024; i++) {
        kpt_virt[i] = (i * 4096) | PAGE_PRESENT | PAGE_WRITE; /* Мапим на физические адреса */
    }
    /* Кладем эту таблицу в самый первый элемент каталога нового процесса */
    dst[0] = kpt_phys | PAGE_PRESENT | PAGE_WRITE;

    /* Копируем верхнюю половину ядра (если там что-то уже есть) */
    /* Каталог страниц процесса наследует все
     * ядерное пространство (выше USER_TOP) напрямую из текущего рабочего каталога страниц.
     * Это автоматически перенесет маппинг фреймбуфера, MMIO и всех структур ядра,
     * настроенных на этапе загрузки, избавляя от хардкода девайсов в VMM.
     */
    size_t kernel_start_idx = USER_TOP >> 22;
    for (size_t i = kernel_start_idx; i < 1024; ++i) {
        dst[i] = kernel_page_directory[i];
    }

    /* Временно мапим первые 64 МБ физической памяти (16 каталогов по 4 МБ),
     * чтобы ядро (1 МБ) и все страницы PMM (выше 16 МБ, включая 0x01002000)
     * были доступны напрямую по своим физическим адресам при включенном пейджинге.
     */
    for (uint32_t k = 0; k < 16; k++) {
        uint32_t pt_phys = pmm_alloc_zeroed();
        if (!pt_phys) return -1;
        uint32_t* pt_virt = table(pt_phys);

        for (uint32_t i = 0; i < 1024; i++) {
            // Мапим физический адрес 1в1 на виртуальный
            pt_virt[i] = ((k * 1024 + i) * 4096) | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        }
        // Кладем таблицу в каталог страниц нового процесса
        dst[k] = pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    g_user.pd_phys = pd_phys;
    g_user_valid = true;

    /* Включаем пейджинг на процессоре */
    __asm__ volatile("mov %0, %%cr3" : : "r"(pd_phys) : "memory");
    puts_com1("Masix: Debug: paging turned on.\n");

    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
    puts_com1("Masix: Debug: paging turned on (x2).\n");

    return 0;
}

int vmm_userspace_clone(uint32_t src_cr3, uint32_t* dst_cr3_out) {
    if (!src_cr3 || !dst_cr3_out) return -1;
    uint32_t* src = table(src_cr3 & PTE_ADDR_MASK); if (!src) return -1;

    uint32_t dst_phys = pmm_alloc_zeroed(); if (!dst_phys) return -1;
    uint32_t* dst = table(dst_phys);

    size_t kernel_start_idx = USER_TOP >> 22;

    /* Динамически копируем ядерное отображение */
    for (size_t i = kernel_start_idx; i < 1024; i++) dst[i] = src[i];

    /* Клонируем только таблицы юзерспейса (все, что ниже границы ядра) */
    for (size_t i = 0; i < kernel_start_idx; i++) {
        uint32_t e = src[i];
        if (!(e & PAGE_PRESENT)) continue;

        uint32_t* src_pt = table(e & PTE_ADDR_MASK);
        uint32_t dst_pt_phys = pmm_alloc_zeroed();
        if (!dst_pt_phys) return -1;
        uint32_t* dst_pt = table(dst_pt_phys);

        for (size_t j = 0; j < 1024; j++) {
            uint32_t pte = src_pt[j];
            if (!(pte & PAGE_PRESENT)) continue;

            uint32_t virt = ((uint32_t)i << 22) | ((uint32_t)j << 12);
            uint32_t page_phys = pte & PTE_ADDR_MASK;

            /* Identity mappings are shared kernel plumbing, not process data. */
            if (page_phys == virt) {
                dst_pt[j] = pte;
                continue;
            }

            uint32_t new_page_phys = pmm_alloc_page();
            if (!new_page_phys) return -1;
            memcpy((void*)new_page_phys, (void*)page_phys, PAGE_SIZE);
            dst_pt[j] = (new_page_phys & PTE_ADDR_MASK) | (pte & ~PTE_ADDR_MASK);
        }
        dst[i] = dst_pt_phys | (e & ~PTE_ADDR_MASK);
    }
    *dst_cr3_out = dst_phys;
    return 0;
}

int vmm_userspace_destroy(uint32_t cr3) {
    if (!cr3) return -1;
    uint32_t* pd = table(cr3 & PAGE_MASK); if (!pd) return -1;

    /* Динамически вычисляем границу юзерспейса */
    size_t kernel_start_idx = USER_TOP >> 22;

    /* Крутим цикл строго до границы ядра, чтобы случайно не удалить его таблицы */
    for (size_t i = 0; i < kernel_start_idx; ++i) {
        uint32_t e = pd[i]; if (!(e & PAGE_PRESENT)) continue;
        uint32_t* pt = table(e & PTE_ADDR_MASK);
        if (pt) {
            for (size_t j = 0; j < 1024; j++) {
                if (pt[j] & PAGE_PRESENT) {
                    uint32_t virt = ((uint32_t)i << 22) | ((uint32_t)j << 12);
                    uint32_t phys = pt[j] & PTE_ADDR_MASK;
                    if (phys != virt) pmm_free_page(phys);
                }
            }
            pmm_free_page(e & PTE_ADDR_MASK);
        }
        pd[i] = 0;
    }
    pmm_free_page(cr3 & PAGE_MASK);
    return 0;
}

uint32_t vmm_user_cr3(void) {
    return g_user_valid ? g_user.pd_phys : 0;
}

void vmm_user_switch(void) {
    if (!g_user_valid) return;
    __asm__ volatile ("mov %0, %%cr3" :: "r"(g_user.pd_phys) : "memory");
}

void vmm_switch_cr3(uint32_t cr3) {
    if (!cr3) return;
    __asm__ volatile ("mov %0, %%cr3" :: "r"(cr3) : "memory");
}

int vmm_kernel_map(uint32_t virt, uint32_t phys, uint32_t flags) {
    /* Проверяем, что адрес находится в зоне ядра (выше USER_TOP) */
    if (virt < USER_TOP || (virt & 0xFFF) || (phys & 0xFFF)) {
        return -1;
    }

    uint32_t pd_idx = idx2(virt);

    /* Если таблицы страниц для этого участка ядра еще нет в kernel_page_directory */
    if (!(kernel_page_directory[pd_idx] & PAGE_PRESENT)) {
        uint32_t pt_phys = pmm_alloc_zeroed();
        if (!pt_phys) return -1;

        kernel_page_directory[pd_idx] = pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    uint32_t* pt = table(kernel_page_directory[pd_idx] & PAGE_MASK);

    /* Прописываем физический адрес девайса в таблицу страниц ядра */
    pt[idx1(virt)] = (phys & PAGE_MASK) | PAGE_PRESENT | PAGE_WRITE | PAGE_USER | flags;

    /* Сбрасываем TLB для этого адреса */
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");

    return 0;
}

