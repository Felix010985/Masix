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
#include <boot.h>
#include <config.h>
extern uint32_t boot_pd[1024];

/* FelixProfi: Я забыл короче удалить вот эти вот дефайны, но надо было бы потому что
#include <stdint.h>
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

static inline uint32_t vmm_idx2(uint32_t v) { return (v >> 22) & 0x3FF; }
static inline uint32_t vmm_idx1(uint32_t v) { return (v >> 12) & 0x3FF; }
static inline bool vmm_canonical_user(uint32_t va) { return va < USER_TOP; }

__attribute__((noinline)) uint32_t* vmm_phys_to_virt(uint32_t phys) {
    if (!phys || (phys & (PAGE_SIZE - 1))) return NULL;
    return (uint32_t*)phys;
}

/* Верхняя граница физической памяти, которую каталог страниц процесса
 * отображает один к одному. Ядро ходит по физическим адресам напрямую
 * (vmm_phys_to_virt), поэтому в отображение обязаны попасть как все
 * страницы PMM, так и данные, которые загрузчик положил в произвольное
 * место: например initramfs. Протокол LINUX (GRUB, Limine) грузит модуль
 * выше 64 МБ, а Multiboot2 - низко, отсюда и разница в поведении.
 * Диапазон берем из карты памяти и границ модулей, округляя до 4 МБ,
 * потому что записи каталога отображают ровно по 4 МБ. */
static uint32_t vmm_identity_limit(void) {
    uint64_t top = 0;

    for (uint32_t i = 0; i < boot_info.mmap_count; i++) {
        if (boot_info.mmap[i].type != BOOT_MEMMAP_USABLE) continue;
        uint64_t end = boot_info.mmap[i].base + boot_info.mmap[i].length;
        if (end > top) top = end;
    }

    for (uint32_t i = 0; i < boot_info.module_count; i++) {
        uint64_t end = (uint64_t)boot_info.modules[i].end;
        if (end > top) top = end;
    }

    /* Карты памяти нет: отображаем хотя бы ядро и первые страницы PMM. */
    if (top == 0) top = 64ULL * 1024 * 1024;

    /* Выше USER_TOP начинается адресное пространство ядра, его отображение
     * приходит из boot_pd и низким быть не должно. */
    if (top > USER_TOP) top = USER_TOP;

    return (uint32_t)((top + 0x3FFFFFULL) & ~0x3FFFFFULL);
}

static uint32_t* vmm_ensure_user_child(uint32_t* parent, uint32_t idx) {
    uint32_t e = parent[idx];
    if (!(e & PAGE_PRESENT)) {
        uint32_t phys = pmm_alloc_zeroed();
        if (!phys) return NULL;
        parent[idx] = phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        return vmm_phys_to_virt(phys);
    }
    debug_puts("Masix: Debug: vmm_ensure_user_child executed.\n");
    return vmm_phys_to_virt(e & PTE_ADDR_MASK);
}

int vmm_user_map(uint32_t virt, uint32_t phys, uint32_t flags) {
    if (!g_user_valid || !vmm_canonical_user(virt) || (virt & 0xFFF) || (phys & 0xFFF))
        return -1;

    uint32_t* pd = vmm_phys_to_virt(g_user.pd_phys);
    if (!pd) return -1;

    uint32_t* pt = vmm_ensure_user_child(pd, vmm_idx2(virt));
    if (!pt) return -1;

    pt[vmm_idx1(virt)] = (phys & PTE_ADDR_MASK) | PAGE_PRESENT | PAGE_USER | (flags & PAGE_WRITE);

    uint32_t current_cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(current_cr3));
    if ((current_cr3 & PAGE_MASK) == (g_user.pd_phys & PAGE_MASK))
        __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");
    debug_puts("Masix: Debug: vmm_user_map executed.\n");

    return 0;
}

int vmm_userspace_create(void) {
    uint32_t pd_phys = pmm_alloc_zeroed();
    if (!pd_phys) return -1;
    { extern void itoa(int,char*,int);
      char b[16]; debug_puts("vmm: pd_phys=0x"); itoa((int)(((unsigned)pd_phys)>>16),b,16); debug_puts(b);
      debug_puts("_"); itoa((int)(((unsigned)pd_phys)&0xFFFF),b,16); debug_puts(b); debug_puts("\n"); }

    uint32_t* dst = vmm_phys_to_virt(pd_phys);

    uint32_t kpt_phys = pmm_alloc_zeroed();
    if (!kpt_phys) return -1;
    uint32_t* kpt_virt = vmm_phys_to_virt(kpt_phys);

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
        dst[i] = boot_pd[i];
    }

    /* Отображаем один к одному всю физическую память, которую ядру
     * может понадобиться увидеть напрямую: код ядра, страницы PMM и
     * данные загрузчика (initramfs). Иначе после переключения на этот
     * каталог обращение к модулю по физическому адресу дает page fault.
     */
    uint32_t identity_pdes = vmm_identity_limit() >> 22;
    for (uint32_t k = 0; k < identity_pdes; k++) {
        uint32_t pt_phys = pmm_alloc_zeroed();
        if (!pt_phys) return -1;
        uint32_t* pt_virt = vmm_phys_to_virt(pt_phys);

        for (uint32_t i = 0; i < 1024; i++) {
            // Мапим физический адрес 1в1 на виртуальный
            pt_virt[i] = ((k * 1024 + i) * 4096) | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
        }
        // Кладем таблицу в каталог страниц нового процесса
        dst[k] = pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    { extern void itoa(int,char*,int); char b[16]; uint32_t e = dst[1012];
      debug_puts("vmm: pd[1012]="); itoa((int)(e>>16),b,16); debug_puts(b); debug_puts("_"); itoa((int)(e&0xFFFF),b,16); debug_puts(b); debug_puts("\n"); }

    g_user.pd_phys = pd_phys;
    g_user_valid = true;

    /* Включаем пейджинг на процессоре */
    __asm__ volatile("mov %0, %%cr3" : : "r"(pd_phys) : "memory");
    debug_puts("Masix: Debug: paging turned on.\n");

    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
    debug_puts("Masix: Debug: paging turned on (x2).\n");

    return 0;
}

int vmm_userspace_clone(uint32_t src_cr3, uint32_t* dst_cr3_out) {
    if (!src_cr3 || !dst_cr3_out) return -1;
    uint32_t* src = vmm_phys_to_virt(src_cr3 & PTE_ADDR_MASK); if (!src) return -1;

    uint32_t dst_phys = pmm_alloc_zeroed(); if (!dst_phys) return -1;
    uint32_t* dst = vmm_phys_to_virt(dst_phys);

    size_t kernel_start_idx = USER_TOP >> 22;

    /* Динамически копируем ядерное отображение */
    for (size_t i = kernel_start_idx; i < 1024; i++) dst[i] = src[i];

    /* Клонируем только таблицы юзерспейса (все, что ниже границы ядра) */
    for (size_t i = 0; i < kernel_start_idx; i++) {
        uint32_t e = src[i];
        if (!(e & PAGE_PRESENT)) continue;

        uint32_t* src_pt = vmm_phys_to_virt(e & PTE_ADDR_MASK);
        uint32_t dst_pt_phys = pmm_alloc_zeroed();
        if (!dst_pt_phys) return -1;
        uint32_t* dst_pt = vmm_phys_to_virt(dst_pt_phys);

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
    { extern void itoa(int,char*,int); char b[16];
      debug_puts("  vmm: destroy cr3=0x"); itoa((int)(((unsigned)cr3)>>16),b,16); debug_puts(b); debug_puts("_"); itoa((int)(((unsigned)cr3)&0xFFFF),b,16); debug_puts(b); debug_puts("\n"); }

    /* Временно переключаемся на boot_pd, чтобы ходить по таблицам
     * страниц по физическим адресам (identity map), даже когда
     * текущий каталог страниц уже не отображает эту память. */
    uint32_t saved_cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(saved_cr3));
    vmm_switch_cr3((uint32_t)boot_pd);

    uint32_t* pd = vmm_phys_to_virt(cr3 & PAGE_MASK);
    if (!pd) {
        vmm_switch_cr3(saved_cr3);
        return -1;
    }

    /*
     * Вычисление индекса границы между пространством пользователя и ядра.
     * В архитектуре Higher-Half это предотвращает случайное удаление
     * или порчу глобальных структур отображения самого ядра.
     */
    size_t kernel_start_idx = USER_TOP >> 22;

    for (size_t i = 0; i < kernel_start_idx; ++i) {
        uint32_t e = pd[i];
        if (!(e & PAGE_PRESENT)) continue;

        uint32_t* pt = vmm_phys_to_virt(e & PTE_ADDR_MASK);
        if (pt) {
            for (size_t j = 0; j < 1024; j++) {
                if (pt[j] & PAGE_PRESENT) {
                    uint32_t virt = ((uint32_t)i << 22) | ((uint32_t)j << 12);
                    uint32_t phys = pt[j] & PTE_ADDR_MASK;

                    /*
                     * Освобождение физических фреймов, выделенных пользователю.
                     * Тождественно отображенные страницы (phys == virt) игнорируются,
                     * чтобы сохранить фиксированные структуры или регистры MMIO.
                     */
                    if (phys != virt) pmm_free_page(phys);
                }
            }
            /* Освобождение физической памяти самой таблицы страниц */
            pmm_free_page(e & PTE_ADDR_MASK);
        }
        pd[i] = 0;
    }

    /* Освобождение физической страницы корневого каталога страниц */
    pmm_free_page(cr3 & PAGE_MASK);

    /* Восстанавливаем каталог страниц, активный до вызова. Иначе
     * sys_execve(), уничтожающий старый образ после загрузки нового,
     * вернулся бы в кольцо 3 с boot_pd - тождественным отображением
     * физической памяти - и процесс стал бы исполнять мусор. */
    vmm_switch_cr3(saved_cr3);
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

    uint32_t pd_idx = vmm_idx2(virt);

    /* Если таблицы страниц для этого участка ядра еще нет в kernel_page_directory */
    if (!(kernel_page_directory[pd_idx] & PAGE_PRESENT)) {
        uint32_t pt_phys = pmm_alloc_zeroed();
        if (!pt_phys) return -1;

        kernel_page_directory[pd_idx] = pt_phys | PAGE_PRESENT | PAGE_WRITE | PAGE_USER;
    }

    uint32_t* pt = vmm_phys_to_virt(kernel_page_directory[pd_idx] & PAGE_MASK);

    /* Прописываем физический адрес девайса в таблицу страниц ядра */
    pt[vmm_idx1(virt)] = (phys & PAGE_MASK) | PAGE_PRESENT | PAGE_WRITE | PAGE_USER | flags;

    /* Сбрасываем TLB для этого адреса */
    __asm__ volatile ("invlpg (%0)" : : "r"(virt) : "memory");

    return 0;
}

/* Отобразить область устройства (видеопамять) в окно MMIO ядра.
 * Работает с активной таблицей страниц, какой бы файл ее ни создал:
 * на этапе загрузки это каталог загрузки, дальше каталог ядра. */
uint32_t vmm_map_mmio(uint32_t phys, uint32_t size) {
    if (phys == 0 || size == 0 || (phys & 0xFFF)) return 0;
    if (size > VMM_MMIO_SIZE) return 0;

    uint32_t pages = (size + PAGE_SIZE - 1) / PAGE_SIZE;
    uint32_t pd_idx = VMM_MMIO_BASE >> 22;

    uint32_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));

    uint32_t* pd = vmm_phys_to_virt(cr3 & PTE_ADDR_MASK);
    if (!pd) return 0;

    uint32_t* pt;
    if (!(pd[pd_idx] & PAGE_PRESENT)) {
        uint32_t pt_phys = pmm_alloc_zeroed();
        if (!pt_phys) return 0;
        pd[pd_idx] = pt_phys | PAGE_PRESENT | PAGE_WRITE;
        pt = vmm_phys_to_virt(pt_phys);
    } else {
        pt = vmm_phys_to_virt(pd[pd_idx] & PTE_ADDR_MASK);
    }
    if (!pt) return 0;

    for (uint32_t i = 0; i < pages; i++) {
        pt[i] = (phys + i * PAGE_SIZE) | PAGE_PRESENT | PAGE_WRITE;
    }

    /* Сбрасываем TLB для отображенных адресов */
    for (uint32_t i = 0; i < pages; i++) {
        __asm__ volatile ("invlpg (%0)" : : "r"(VMM_MMIO_BASE + i * PAGE_SIZE) : "memory");
    }

    return VMM_MMIO_BASE;
}

