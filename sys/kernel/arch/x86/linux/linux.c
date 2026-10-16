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
#include <boot/linux.h>

/* Разбор нулевой страницы протокола LINUX.
 * addr это физический адрес struct boot_params, пришедший в ESI.
 *
 * Признаком протокола считаем заполненную таблицу e820 или подпись "HdrS"
 * в заголовке setup: загрузчики кладут заголовок не полностью, а вот карту
 * памяти и initrd заполняют всегда. */

void linux_gather(uint32_t addr) {
    memset(&boot_info, 0, sizeof(boot_info));
    boot_info.protocol = BOOT_PROTOCOL_LINUX;

    if (addr == 0) {
        debug_puts("Masix: linux boot params pointer is NULL\n");
        return;
    }

    struct linux_boot_params* params = (struct linux_boot_params*)(uintptr_t)addr;

    int has_header = (params->hdr.header == LINUX_SETUP_MAGIC);
    int has_map = (params->e820_entries != 0 && params->e820_entries <= 128);

    if (!has_header && !has_map) {
        debug_puts("Masix: linux boot params look empty\n");
        return;
    }

    /* Командная строка ядра. Адрес физический, память отображена один к одному. */
    if (params->hdr.cmd_line_ptr != 0) {
        const char* cmdline = (const char*)(uintptr_t)params->hdr.cmd_line_ptr;
        size_t i = 0;
        for (; i + 1 < sizeof(boot_info.cmdline); i++) {
            if (cmdline[i] == 0) break;
            boot_info.cmdline[i] = cmdline[i];
        }
        boot_info.cmdline[i] = 0;
    }

    /* initrd из протокола LINUX: физический адрес и размер. */
    if (params->hdr.ramdisk_image != 0 && params->hdr.ramdisk_size != 0 &&
        boot_info.module_count < BOOT_MODULE_MAX) {
        struct boot_module* mod = &boot_info.modules[boot_info.module_count++];
        mod->start = params->hdr.ramdisk_image;
        mod->end = params->hdr.ramdisk_image + params->hdr.ramdisk_size;
        mod->cmdline[0] = 0;
    }

    /* Карта памяти e820. */
    if (has_map) {
        for (uint32_t i = 0; i < params->e820_entries; i++) {
            struct linux_e820_entry* e = &params->e820_table[i];

            if (e->size == 0) continue;
            if (boot_info.mmap_count >= BOOT_MEMMAP_MAX) break;

            struct boot_memmap_entry* out = &boot_info.mmap[boot_info.mmap_count++];
            out->base = e->addr;
            out->length = e->size;
            out->type = e->type;
        }
    }

    /* Сведения EFI, если загрузчик под UEFI. */
    if (params->efi_info.efi_loader_signature == 0x20454946 &&   /* "EFI " */
        params->efi_info.efi_systab != 0) {
        boot_info.firmware = BOOT_FIRMWARE_EFI;
        boot_info.efi_system_table = params->efi_info.efi_systab;

        if (params->efi_info.efi_systab_hi != 0) {
            boot_info.efi_system_table |= (uint64_t)params->efi_info.efi_systab_hi << 32;
        }
    }

    if (boot_info.mmap_count > 0) {
        boot_memmap_summary();
    } else {
        /* Карты памяти нет: берем размеры старыми полями протокола. */
        uint32_t alt = params->alt_mem_k;
        if (alt != 0) {
            boot_info.mem_lower = 1024;
            boot_info.mem_upper = alt;
        } else {
            boot_info.mem_lower = 640;
            boot_info.mem_upper = 0;
        }
    }
}
