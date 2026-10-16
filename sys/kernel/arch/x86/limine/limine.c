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
#include <boot/limine.h>

/* Точка входа для Limine. Limine требует ядро в верхней половине,
 * поэтому у протокола своя точка входа (файл arch/x86/i686/entry.c),
 * а низкий бутстрап остается загрузчикам с плоскими адресами:
 * multiboot2 (GRUB) и протокол linux. */
extern void entry_limine_start(void);

/* Метка начала области запросов. Загрузчик ищет запросы между метками,
 * поэтому порядок секций задается в скрипте линковки. */
__attribute__((section(".limine_requests_start_marker"), used, aligned(8)))
uint64_t limine_requests_start_marker[4] = LIMINE_REQUESTS_START_MARKER;

/* Запрашиваемая базовая ревисия протокола. Третье поле обнуляет загрузчик,
 * если ревисия поддерживается; иначе там остается наше число, и ядро об этом
 * сообщает в отладочный вывод, но продолжает работу. */
__attribute__((section(".limine_requests_start_marker"), used, aligned(8)))
uint64_t limine_base_revision[3] = {
    LIMINE_BASE_REVISION_MAGIC_0,
    LIMINE_BASE_REVISION_MAGIC_1,
    2
};

__attribute__((section(".limine_requests"), used, aligned(8)))
struct limine_hhdm_request limine_hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
    .response = 0
};

__attribute__((section(".limine_requests"), used, aligned(8)))
struct limine_firmware_type_request limine_firmware_type_request = {
    .id = LIMINE_FIRMWARE_TYPE_REQUEST_ID,
    .revision = 0,
    .response = 0
};

__attribute__((section(".limine_requests"), used, aligned(8)))
struct limine_efi_system_table_request limine_efi_system_table_request = {
    .id = LIMINE_EFI_SYSTEM_TABLE_REQUEST_ID,
    .revision = 0,
    .response = 0
};

__attribute__((section(".limine_requests"), used, aligned(8)))
struct limine_memmap_request limine_memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST_ID,
    .revision = 0,
    .response = 0
};

__attribute__((section(".limine_requests"), used, aligned(8)))
struct limine_module_request limine_module_request = {
    .id = LIMINE_MODULE_REQUEST_ID,
    .revision = 0,
    .response = 0
};

__attribute__((section(".limine_requests"), used, aligned(8)))
struct limine_entry_point_request limine_entry_point_request = {
    .id = LIMINE_ENTRY_POINT_REQUEST_ID,
    .revision = 0,
    .response = 0,
    .entry = entry_limine_start
};

__attribute__((section(".limine_requests"), used, aligned(8)))
struct limine_kernel_file_request limine_kernel_file_request = {
    .id = LIMINE_KERNEL_FILE_REQUEST_ID,
    .revision = 0,
    .response = 0
};

__attribute__((section(".limine_requests"), used, aligned(8)))
struct limine_stack_size_request limine_stack_size_request = {
    .id = LIMINE_STACK_SIZE_REQUEST_ID,
    .revision = 0,
    .response = 0,
    .stack_size = 64 * 1024
};

__attribute__((section(".limine_requests_end_marker"), used, aligned(8)))
uint64_t limine_requests_end_marker[2] = LIMINE_REQUESTS_END_MARKER;

/* Смещение прямого отображения верхней половины, его сообщил загрузчик. */
static uint64_t limine_hhdm_offset;

/* Указатель на ответ. Пока работают страницы загрузчика, область HHDM
 * отображена, а для 32-битного ядра HHDM обязан лежать ниже 4 ГБ,
 * поэтому достаточно младших 32 бит значения. */
static void* limine_response_ptr(uint64_t value) {
    if (value == 0) return NULL;
    if (value > 0xFFFFFFFFULL) return NULL;
    return (void*)(uintptr_t)value;
}

/* Физический адрес для данных, которые ядро читает уже своими силами
 * (модули, таблица EFI). Физический адрес равен значению минус HHDM. */
static uintptr_t limine_physical_ptr(uint64_t value) {
    if (value == 0) return 0;
    if (value < limine_hhdm_offset) return 0;

    uint64_t phys = value - limine_hhdm_offset;
    if (phys > 0xFFFFFFFFULL) return 0;
    return (uintptr_t)phys;
}

/* Перевод типа участка памяти LIMINE в нумерацию e820 из <boot.h>. */
static uint32_t limine_mem_type(uint64_t type) {
    switch (type) {
        case LIMINE_MEMMAP_USABLE:
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE:
            return BOOT_MEMMAP_USABLE;
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE: return BOOT_MEMMAP_ACPI_RECLAIM;
        case LIMINE_MEMMAP_ACPI_NVS:         return BOOT_MEMMAP_ACPI_NVS;
        case LIMINE_MEMMAP_BAD_MEMORY:       return BOOT_MEMMAP_BAD;
        default:                             return BOOT_MEMMAP_RESERVED;
    }
}

uintptr_t limine_ptr_to_kernel(uint64_t value) {
    /* Физический адрес: он же адрес в тождественном отображении ядра. */
    return limine_physical_ptr(value);
}

void limine_gather(void) {
    memset(&boot_info, 0, sizeof(boot_info));
    boot_info.protocol = BOOT_PROTOCOL_LIMINE;

    /* Базовая ревисия. Несовпадение не фатально: ядру хватает ревизии 2,
     * а более старый загрузчик отдаст ответы в том же формате. */
    if (limine_base_revision[2] != 0) {
        debug_puts("Masix: limine runs with an older base revision\n");
    }

    /* Смещение HHDM нужно раньше всех: без него указатели ответов не разобрать. */
    struct limine_hhdm_response* hhdm = limine_response_ptr(limine_hhdm_request.response);
    if (hhdm == NULL || hhdm->offset > 0xFFFFFFFFULL || (hhdm->offset & 0xFFF) != 0) {
        debug_puts("Masix: limine HHDM is not usable on 32-bit\n");
        return;
    }
    limine_hhdm_offset = hhdm->offset;

    /* Тип прошивки: BIOS ведет к VBE, EFI ведет к GOP. */
    struct limine_firmware_type_response* firmware = limine_response_ptr(limine_firmware_type_request.response);
    if (firmware != NULL) {
        if (firmware->firmware_type == LIMINE_FIRMWARE_TYPE_X86BIOS) {
            boot_info.firmware = BOOT_FIRMWARE_BIOS;
        } else {
            boot_info.firmware = BOOT_FIRMWARE_EFI;
        }
    }

    /* Таблица EFI System Table, если прошивка UEFI. */
    struct limine_efi_system_table_response* st = limine_response_ptr(limine_efi_system_table_request.response);
    if (st != NULL) {
        uintptr_t phys = limine_physical_ptr(st->address);
        if (phys != 0) {
            boot_info.efi_system_table = phys;
            boot_info.firmware = BOOT_FIRMWARE_EFI;
        }
    }

    /* Карта памяти. */
    struct limine_memmap_response* mmap = limine_response_ptr(limine_memmap_request.response);
    if (mmap != NULL && mmap->entry_count <= 256) {
        /* entries это массив указателей, каждый указатель 64 битный. */
        uint64_t* entries = limine_response_ptr(mmap->entries);

        if (entries != NULL) {
            for (uint64_t i = 0; i < mmap->entry_count; i++) {
                struct limine_memmap_entry* e = limine_response_ptr(entries[i]);
                if (e == NULL || e->length == 0) continue;
                if (boot_info.mmap_count >= BOOT_MEMMAP_MAX) break;

                struct boot_memmap_entry* out = &boot_info.mmap[boot_info.mmap_count++];
                out->base = e->base;
                out->length = e->length;
                out->type = limine_mem_type(e->type);
            }
        }
    }

    /* Модули (initramfs). */
    struct limine_module_response* modules = limine_response_ptr(limine_module_request.response);
    if (modules != NULL && modules->module_count <= 16) {
        /* modules это массив 64-битных указателей на struct limine_file. */
        uint64_t* list = limine_response_ptr(modules->modules);

        if (list != NULL) {
            for (uint64_t i = 0; i < modules->module_count; i++) {
                struct limine_file* file = limine_response_ptr(list[i]);
                if (file == NULL || file->size == 0) continue;
                if (boot_info.module_count >= BOOT_MODULE_MAX) break;

                uintptr_t start = limine_physical_ptr(file->address);
                if (start == 0) continue;

                struct boot_module* mod = &boot_info.modules[boot_info.module_count++];
                mod->start = start;
                mod->end = start + (uintptr_t)file->size;
                mod->cmdline[0] = 0;
            }
        }
    }

    if (boot_info.mmap_count > 0) {
        boot_memmap_summary();
    } else {
        /* Карты памяти нет: ядро не сможет работать без PMM, сообщаем. */
        debug_puts("Masix: limine memory map missing\n");
        boot_info.mem_lower = 640;
        boot_info.mem_upper = 0;
    }
}
