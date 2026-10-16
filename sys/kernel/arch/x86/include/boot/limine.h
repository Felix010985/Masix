/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef ARCH_X86_BOOT_LIMINE_H
#define ARCH_X86_BOOT_LIMINE_H

#include <stdint.h>

/* Протокол LIMINE. Описания взяты из спецификации протокола,
 * ничего своего здесь нет. По протоколу все указатели 64-битные:
 * загрузчик пишет в них виртуальный адрес, равный HHDM плюс физический.
 * Для 32-битного ядра такое значение приводится к 32 битам функцией
 * limine_ptr_to_kernel(), которая одновременно проверяет диапазон. */

#define LIMINE_COMMON_MAGIC_0 0xc7b1dd30df4c8b88
#define LIMINE_COMMON_MAGIC_1 0x0a82e883a194f07b

/* Маркеры начала и конца области запросов. */
#define LIMINE_REQUESTS_START_MARKER { 0xf6b8f4b39de7d1ae, 0xfab91a6940fcb9cf, \
                                       0x785c6ed015d3e316, 0x181e920a7852b9d9 }
#define LIMINE_REQUESTS_END_MARKER { 0xadc0e0531bb10d03, 0x9572709f31764c62 }

/* Метка базовой ревизии протокола. */
#define LIMINE_BASE_REVISION_MAGIC_0 0xf9562b2d5c95a6c8
#define LIMINE_BASE_REVISION_MAGIC_1 0x6a7b384944536bdc

/* Тип прошивки, значения как в спецификации. */
#define LIMINE_FIRMWARE_TYPE_X86BIOS 0
#define LIMINE_FIRMWARE_TYPE_UEFI32  1
#define LIMINE_FIRMWARE_TYPE_UEFI64  2
#define LIMINE_FIRMWARE_TYPE_SBI     3

/* Типы участков карты памяти, значения как в спецификации. */
#define LIMINE_MEMMAP_USABLE                 0
#define LIMINE_MEMMAP_RESERVED               1
#define LIMINE_MEMMAP_ACPI_RECLAIMABLE       2
#define LIMINE_MEMMAP_ACPI_NVS               3
#define LIMINE_MEMMAP_BAD_MEMORY             4
#define LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE 5
#define LIMINE_MEMMAP_EXECUTABLE_AND_MODULES  6
#define LIMINE_MEMMAP_FRAMEBUFFER            7

/* Идентификаторы запросов. */
#define LIMINE_HHDM_REQUEST_ID \
    { LIMINE_COMMON_MAGIC_0, LIMINE_COMMON_MAGIC_1, 0x48dcf1cb8ad2b852, 0x63984e959a98244b }

#define LIMINE_FIRMWARE_TYPE_REQUEST_ID \
    { LIMINE_COMMON_MAGIC_0, LIMINE_COMMON_MAGIC_1, 0x8c2f75d90bef28a8, 0x7045a4688eac00c3 }

#define LIMINE_EFI_SYSTEM_TABLE_REQUEST_ID \
    { LIMINE_COMMON_MAGIC_0, LIMINE_COMMON_MAGIC_1, 0x5ceba5163eaaf6d6, 0x0a6981610cf65fcc }

#define LIMINE_MEMMAP_REQUEST_ID \
    { LIMINE_COMMON_MAGIC_0, LIMINE_COMMON_MAGIC_1, 0x67cf3d9d378a806f, 0xe304acdfc50c3c62 }

#define LIMINE_MODULE_REQUEST_ID \
    { LIMINE_COMMON_MAGIC_0, LIMINE_COMMON_MAGIC_1, 0x3e7e279702be32af, 0xca1c4f3bd1280cee }

#define LIMINE_ENTRY_POINT_REQUEST_ID \
    { LIMINE_COMMON_MAGIC_0, LIMINE_COMMON_MAGIC_1, 0x13d86c035a1cd3e1, 0x2b0caa89d8f3026a }

#define LIMINE_KERNEL_FILE_REQUEST_ID \
    { LIMINE_COMMON_MAGIC_0, LIMINE_COMMON_MAGIC_1, 0xad97e90e83f1ed67, 0x31eb5d1c5ff23b69 }

#define LIMINE_STACK_SIZE_REQUEST_ID \
    { LIMINE_COMMON_MAGIC_0, LIMINE_COMMON_MAGIC_1, 0x224ef0460a8e8926, 0xe1cb0fc25f46ea3d }

struct limine_uuid {
    uint32_t a;
    uint16_t b;
    uint16_t c;
    uint8_t d[8];
};

struct limine_file {
    uint64_t revision;
    uint64_t address;
    uint64_t size;
    uint64_t path;
    uint64_t string;
    uint32_t media_type;
    uint32_t unused;
    uint8_t tftp_ipv4[4];
    uint32_t tftp_port;
    uint32_t partition_index;
    uint32_t mbr_disk_id;
    struct limine_uuid gpt_disk_uuid;
    struct limine_uuid gpt_part_uuid;
    struct limine_uuid part_uuid;
};

struct limine_hhdm_response {
    uint64_t revision;
    uint64_t offset;
};

struct limine_hhdm_request {
    uint64_t id[4];
    uint64_t revision;
    uint64_t response;
};

struct limine_firmware_type_response {
    uint64_t revision;
    uint64_t firmware_type;
};

struct limine_firmware_type_request {
    uint64_t id[4];
    uint64_t revision;
    uint64_t response;
};

struct limine_efi_system_table_response {
    uint64_t revision;
    uint64_t address;
};

struct limine_efi_system_table_request {
    uint64_t id[4];
    uint64_t revision;
    uint64_t response;
};

struct limine_memmap_entry {
    uint64_t base;
    uint64_t length;
    uint64_t type;
};

struct limine_memmap_response {
    uint64_t revision;
    uint64_t entry_count;
    uint64_t entries;
};

struct limine_memmap_request {
    uint64_t id[4];
    uint64_t revision;
    uint64_t response;
};

struct limine_module_response {
    uint64_t revision;
    uint64_t module_count;
    uint64_t modules;
};

struct limine_module_request {
    uint64_t id[4];
    uint64_t revision;
    uint64_t response;
};

/* Тип указателя совпадает с разрядностью сборки, поэтому поле входа объявлено
 * функциональным указателем: так его получается заполнить именем функции
 * еще на этапе компиляции, не скачивая адрес в целое число. */
typedef void (*limine_entry_fn)(void);

struct limine_entry_point_request {
    uint64_t id[4];
    uint64_t revision;
    uint64_t response;
    limine_entry_fn entry;
};

struct limine_kernel_file_response {
    uint64_t revision;
    uint64_t executable_file;
};

struct limine_kernel_file_request {
    uint64_t id[4];
    uint64_t revision;
    uint64_t response;
};

struct limine_stack_size_request {
    uint64_t id[4];
    uint64_t revision;
    uint64_t response;
    uint64_t stack_size;
};

/* Запросы ядра. Живут в своем сегменте образа, загрузчик находит их там. */
extern uint64_t limine_base_revision[3];
extern uint64_t limine_requests_start_marker[4];
extern uint64_t limine_requests_end_marker[2];
extern struct limine_hhdm_request limine_hhdm_request;
extern struct limine_firmware_type_request limine_firmware_type_request;
extern struct limine_efi_system_table_request limine_efi_system_table_request;
extern struct limine_memmap_request limine_memmap_request;
extern struct limine_module_request limine_module_request;
extern struct limine_entry_point_request limine_entry_point_request;
extern struct limine_kernel_file_request limine_kernel_file_request;
extern struct limine_stack_size_request limine_stack_size_request;

/* Преобразовать указатель протокола LIMINE в адрес ядра.
 * limine_hhdm_offset это смещение прямого отображения верхней половины,
 * его сообщает ответ на запрос HHDM. Возвращает 0, если адрес не влезает
 * в 32 бита: такое ядро без машины с PAE использовать не может. */
uintptr_t limine_ptr_to_kernel(uint64_t value);

#endif /* ARCH_X86_BOOT_LIMINE_H */
