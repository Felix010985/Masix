/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef ARCH_X86_BOOT_MULTIBOOT2_H
#define ARCH_X86_BOOT_MULTIBOOT2_H

#include <stdint.h>

/* Протокол Multiboot2 (GRUB). Объявления тегов общие для 32-бит и 64-бит:
 * все поля тегов имеют фиксированную ширину, поэтому разрядность не важна. */

#define MULTIBOOT2_MAGIC 0x36D76289

/* Размер области, в которой GRUB ищет заголовок Multiboot2 (в байтах). */
#define MULTIBOOT2_SEARCH 32768

/* Теги информационной структуры. */
#define MULTIBOOT2_TAG_END              0
#define MULTIBOOT2_TAG_CMDLINE          1
#define MULTIBOOT2_TAG_BOOT_LOADER_NAME 2
#define MULTIBOOT2_TAG_MODULE           3
#define MULTIBOOT2_TAG_BASIC_MEMINFO    4
#define MULTIBOOT2_TAG_BOOTDEV          5
#define MULTIBOOT2_TAG_MMAP             6
#define MULTIBOOT2_TAG_VBE              7
#define MULTIBOOT2_TAG_FRAMEBUFFER      8
#define MULTIBOOT2_TAG_ELF_SECTIONS     9
#define MULTIBOOT2_TAG_APM              10
#define MULTIBOOT2_TAG_EFI32            11
#define MULTIBOOT2_TAG_EFI64            12
#define MULTIBOOT2_TAG_SMBIOS           13
#define MULTIBOOT2_TAG_ACPI_OLD         14
#define MULTIBOOT2_TAG_ACPI_NEW         15
#define MULTIBOOT2_TAG_NETWORK          16
#define MULTIBOOT2_TAG_EFI_MMAP         17
#define MULTIBOOT2_TAG_EFI_BS           18
#define MULTIBOOT2_TAG_EFI32_IH         19
#define MULTIBOOT2_TAG_EFI64_IH         20
#define MULTIBOOT2_TAG_LOAD_BASE_ADDR   21

/* Типы участков карты памяти (нумерация Multiboot2). */
#define MULTIBOOT2_MEMORY_AVAILABLE 1
#define MULTIBOOT2_MEMORY_RESERVED  2
#define MULTIBOOT2_MEMORY_ACPI_RECLAIMABLE 3
#define MULTIBOOT2_MEMORY_NVS       4
#define MULTIBOOT2_MEMORY_BADRAM    5

/* Теги заголовка образа. */
#define MULTIBOOT2_HEADER_TAG_END         0
#define MULTIBOOT2_HEADER_TAG_I386_INFO   1
#define MULTIBOOT2_HEADER_TAG_ADDRESS     2
#define MULTIBOOT2_HEADER_TAG_ENTRY       3
#define MULTIBOOT2_HEADER_TAG_EFI_I386    8
#define MULTIBOOT2_HEADER_TAG_EFI_AMD64   9
#define MULTIBOOT2_HEADER_TAG_EFI_BS      7

struct multiboot_tag {
    uint32_t type;
    uint32_t size;
};

struct multiboot_tag_string {
    uint32_t type;
    uint32_t size;
    char string[];
};

struct multiboot_tag_basic_meminfo {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;
    uint32_t mem_upper;
};

struct multiboot_tag_module {
    uint32_t type;
    uint32_t size;
    uint32_t mod_start;
    uint32_t mod_end;
    char string[];
};

struct multiboot_mmap_entry {
    uint64_t addr;
    uint64_t len;
    uint32_t type;
    uint32_t zero;
};

struct multiboot_tag_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t entry_size;
    uint32_t entry_version;
    struct multiboot_mmap_entry entries[];
};

struct multiboot_tag_efi32 {
    uint32_t type;
    uint32_t size;
    uint32_t pointer;
};

struct multiboot_tag_efi64 {
    uint32_t type;
    uint32_t size;
    uint64_t pointer;
};

struct multiboot_tag_efi_bs {
    uint32_t type;
    uint32_t size;
};

struct multiboot_tag_load_base_addr {
    uint32_t type;
    uint32_t size;
    uint32_t load_base_addr;
};

#endif /* ARCH_X86_BOOT_MULTIBOOT2_H */

/* Тег с картой памяти в формате EFI. GRUB отдает его вместо обычной
 * карты, когда ядро просит сохранить boot services (тег 7 заголовка). */
struct multiboot_tag_efi_mmap {
    uint32_t type;
    uint32_t size;
    uint32_t descr_size;
    uint32_t descr_vers;
    uint8_t efi_mmap[];
};

/* Описатель области EFI. Раскладка одна и та же для 32-бит и 64-бит. */
struct multiboot_efi_memory_desc {
    uint32_t type;
    uint32_t pad;
    uint64_t physical_start;
    uint64_t virtual_start;
    uint64_t num_pages;
    uint64_t attribute;
};

/* Типы областей EFI. */
#define MULTIBOOT_EFI_CONVENTIONAL  1
#define MULTIBOOT_EFI_UNUSABLE      2
#define MULTIBOOT_EFI_ACPI_RECLAIM  3
#define MULTIBOOT_EFI_ACPI_NVS      4
#define MULTIBOOT_EFI_MMIO          5
#define MULTIBOOT_EFI_MMIO_PORTS    6
#define MULTIBOOT_EFI_PAL_CODE      7
#define MULTIBOOT_EFI_PERSISTENT    8
#define MULTIBOOT_EFI_LOADER_CODE   9
#define MULTIBOOT_EFI_LOADER_DATA   10
#define MULTIBOOT_EFI_LOADER_BOOT   11
#define MULTIBOOT_EFI_RUNTIME       12
