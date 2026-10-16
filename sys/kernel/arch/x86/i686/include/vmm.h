/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef VMM_H
#define VMM_H

#include <stdint.h>

#define PAGE_SIZE 4096
#define PAGE_MASK 0xFFFFF000

/* Флаги управления страницами i686 (битовые маски процессора) */
#define PAGE_PRESENT  0x1  /* Страница находится в физической памяти (PTE_P) */
#define PAGE_WRITE    0x2  /* Разрешена запись на страницу (PTE_W) */
#define PAGE_USER     0x4  /* Страница доступна для Ring 3 / User Mode (PTE_U) */

#define PTE_ADDR_MASK 0xFFFFF000 /* Битовая маска для выделения чистого физического адреса памяти из записи таблицы страниц */
#define USER_TOP      0xC0000000 /* Верхняя граница памяти пользователя */

uint32_t* vmm_phys_to_virt(uint32_t phys);

/* Создает новое независимое пространство страниц для процесса и шаблонизирует туда ядро */
int vmm_userspace_create(void);

/* Мапит физическую страницу (phys) на виртуальный адрес пользователя (virt) с флагами */
int vmm_user_map(uint32_t virt, uint32_t phys, uint32_t flags);

/* Убирает маппинг с виртуального адреса (virt) и возвращает физический адрес */
int vmm_user_unmap(uint32_t virt, uint32_t* phys_out);

/* Клонирует текущее пространство страниц в новое (для системного вызова fork) */
int vmm_userspace_clone(uint32_t src_cr3, uint32_t* dst_cr3_out);

/* Полностью очищает и уничтожает таблицы страниц процесса при его завершении */
int vmm_userspace_destroy(uint32_t cr3);

/* Переключает процессор на текущую активную директорию страниц пользователя */
void vmm_user_switch(void);

/* Switch to an already-owned address space. */
void vmm_switch_cr3(uint32_t cr3);

/* Возвращает физический адрес текущего каталога страниц процесса (для загрузки в CR3) */
uint32_t vmm_user_cr3(void);

/* Проверяет, валиден ли указанный диапазон виртуальной памяти и есть ли к нему права */
int vmm_user_range_ok(uint32_t virt, uint32_t len, int write);

/* Переводит виртуальный адрес процесса в физический и возвращает саму запись PTE */
int vmm_user_translate(uint32_t virt, uint32_t* phys_out, uint32_t* pte_out);

/* Меняет флаги доступа (например, права на запись) для конкретного виртуального адреса */
int vmm_user_protect(uint32_t virt, uint32_t flags);

/* Мапит физический регион (девайсы, фреймбуфер, MMIO) напрямую в пространство ядра */
int vmm_kernel_map(uint32_t virt, uint32_t phys, uint32_t flags);

/* Окно, в которое отображаются области устройств (видеопамять).
 * Лежит в старшем гигабайте и не пересекается ни с ядром (0xC0000000),
 * ни с тождественным отображением физической памяти. */
#define VMM_MMIO_BASE 0xE8000000
#define VMM_MMIO_SIZE 0x01000000

/* Отобразить область устройства phys размером size байт в окно MMIO.
 * Возвращает виртуальный адрес или 0, если область не поместилась. */
uint32_t vmm_map_mmio(uint32_t phys, uint32_t size);

#endif
