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
#include <boot/multiboot2.h>

/* Точка входа 32-битного бутстрапа (файл arch/x86/i686/entry.c).
 * Физический адрес этой функции и есть точка входа для загрузчика. */
extern void entry_start(void);

/* Размеры тегов заголовка. Считаем числами, потому что в самом заголовке
 * нужны поле длины и контрольная сумма, а sizeof() на себя не сослаться. */
#define MB2_MAGIC     0xE85250D6
#define MB2_ARCH      0            /* архитектура i386 */
/* Каждый тег заголовка начинается с 8-байтной шапки: тип 16 бит,
 * флаги 16 бит, размер 32 бита. Размер каждого тега делаем кратным 8,
 * иначе следующий тег встанет не по границе и загрузчик его не прочтет. */
#define MB2_TAG_INFO_SIZE  32      /* 8 + 6 * 4 */
#define MB2_TAG_ENTRY_SIZE 16      /* 8 + 4 + 4 добивки */
#define MB2_TAG_EFI_BS_SIZE 8
#define MB2_TAG_END_SIZE    8
#define MB2_FIXED_SIZE      16      /* magic, architecture, length, checksum */

#define MB2_HEADER_LEN (MB2_FIXED_SIZE + MB2_TAG_INFO_SIZE + MB2_TAG_ENTRY_SIZE + \
                        MB2_TAG_EFI_BS_SIZE + MB2_TAG_END_SIZE)

#define MB2_CHECKSUM ((uint32_t)(-(int32_t)(MB2_MAGIC + MB2_ARCH + MB2_HEADER_LEN)))

/* Теги заголовка используют свою шапку: тип 16 бит, флаги 16 бит, размер 32 бита. */
struct multiboot2_header_info_request {
    uint16_t type;
    uint16_t flags;
    uint32_t size;
    uint32_t mbi_tag_types[6];
};

struct multiboot2_header_entry_address {
    uint16_t type;
    uint16_t flags;
    uint32_t size;
    uint32_t entry_addr;
    uint32_t pad;
};

struct multiboot2_header_efi_bs {
    uint16_t type;
    uint16_t flags;
    uint32_t size;
};

struct multiboot2_header_end {
    uint16_t type;
    uint16_t flags;
    uint32_t size;
};

struct multiboot2_header {
    uint32_t magic;
    uint32_t architecture;
    uint32_t header_length;
    uint32_t checksum;
    struct multiboot2_header_info_request info_request;
    struct multiboot2_header_entry_address entry_address;
    struct multiboot2_header_efi_bs efi_bs;
    struct multiboot2_header_end end;
} __attribute__((packed));

/* Заголовок GRUB. Специально нет тега фреймбуфера: ядро само запрашивает
 * видеорежим у VBE или GOP, поэтому загрузчик не должен ничего включать.
 * Тег EFI boot services нужен для GRUB, работающего через UEFI: тогда в ядро
 * входят с живыми boot services, и режим можно не только прочитать у GOP,
 * но и включить. Взамен GRUB отдает карту памяти в виде EFI, ее ядро
 * разбирает само. Тег EFI i386 entry address не используется: GRUB его
 * не понимает, а вход по ELF и по тегу entry address и так приводит
 * в наш бутстрап. */
__attribute__((section(".multiboot2"), used, aligned(8)))
const struct multiboot2_header multiboot2_header = {
    .magic = MB2_MAGIC,
    .architecture = MB2_ARCH,
    .header_length = MB2_HEADER_LEN,
    .checksum = MB2_CHECKSUM,

    .info_request = {
        .type = 1,
        .flags = 1,          /* тег необязательный: на BIOS нет тегов EFI */
        .size = MB2_TAG_INFO_SIZE,
        .mbi_tag_types = {
            MULTIBOOT2_TAG_CMDLINE,
            MULTIBOOT2_TAG_BOOT_LOADER_NAME,
            MULTIBOOT2_TAG_BASIC_MEMINFO,
            MULTIBOOT2_TAG_MMAP,
#if defined(__x86_64__)
            MULTIBOOT2_TAG_EFI64,
#else
            MULTIBOOT2_TAG_EFI32,
#endif
            MULTIBOOT2_TAG_EFI_BS
        }
    },

    .entry_address = {
        .type = 3,
        .flags = 0,
        .size = MB2_TAG_ENTRY_SIZE,
        .entry_addr = (uint32_t)(uintptr_t)&entry_start
    },

    .efi_bs = {
        .type = 7,
        .flags = 0,
        .size = MB2_TAG_EFI_BS_SIZE
    },

    .end = {
        .type = 0,
        .flags = 0,
        .size = MB2_TAG_END_SIZE
    }
};

/* Строка загрузчика: копируем не больше размера под нее. */
static void multiboot2_copy_str(char* dst, size_t dst_size, const char* src, uint32_t size) {
    if (!src || size == 0) return;

    size_t i = 0;
    for (; i + 1 < dst_size && i + 1 < size; i++) {
        if (src[i] == 0) break;
        dst[i] = src[i];
    }
    dst[i] = 0;
}

/* Перевести тип области EFI в нумерацию e820 из <boot.h>. */
static uint32_t multiboot2_efi_mem_type(uint32_t type) {
    switch (type) {
        case MULTIBOOT_EFI_CONVENTIONAL:
        case MULTIBOOT_EFI_PERSISTENT:
        case MULTIBOOT_EFI_LOADER_CODE:
        case MULTIBOOT_EFI_LOADER_DATA:
        case MULTIBOOT_EFI_LOADER_BOOT:
            return BOOT_MEMMAP_USABLE;
        case MULTIBOOT_EFI_ACPI_RECLAIM: return BOOT_MEMMAP_ACPI_RECLAIM;
        case MULTIBOOT_EFI_ACPI_NVS:     return BOOT_MEMMAP_ACPI_NVS;
        case MULTIBOOT_EFI_UNUSABLE:     return BOOT_MEMMAP_BAD;
        default:                         return BOOT_MEMMAP_RESERVED;
    }
}

/* Перевести тип участка Multiboot2 в нумерацию e820 из <boot.h>. */
static uint32_t multiboot2_mem_type(uint32_t type) {
    switch (type) {
        case MULTIBOOT2_MEMORY_AVAILABLE:       return BOOT_MEMMAP_USABLE;
        case MULTIBOOT2_MEMORY_ACPI_RECLAIMABLE: return BOOT_MEMMAP_ACPI_RECLAIM;
        case MULTIBOOT2_MEMORY_NVS:             return BOOT_MEMMAP_ACPI_NVS;
        case MULTIBOOT2_MEMORY_BADRAM:          return BOOT_MEMMAP_BAD;
        default:                                return BOOT_MEMMAP_RESERVED;
    }
}

void multiboot2_gather(uint32_t magic, uint32_t addr) {
    memset(&boot_info, 0, sizeof(boot_info));
    boot_info.protocol = BOOT_PROTOCOL_MULTIBOOT2;

    if (magic != MULTIBOOT2_MAGIC || addr == 0) {
        debug_puts("Masix: multiboot2 magic mismatch, no boot information\n");
        return;
    }

    struct multiboot_tag* tag = (struct multiboot_tag*)(uintptr_t)(addr + 8);
    uint8_t efi_bs_seen = 0;

    while (1) {
        if (tag->type == MULTIBOOT2_TAG_END) break;

        switch (tag->type) {
            case MULTIBOOT2_TAG_CMDLINE: {
                struct multiboot_tag_string* s = (struct multiboot_tag_string*)tag;
                multiboot2_copy_str(boot_info.cmdline, sizeof(boot_info.cmdline),
                                    s->string, tag->size);
                break;
            }

            case MULTIBOOT2_TAG_BOOT_LOADER_NAME:
                /* имя загрузчика в ядре не хранится, оно не нужно */
                break;

            case MULTIBOOT2_TAG_BASIC_MEMINFO: {
                struct multiboot_tag_basic_meminfo* m = (struct multiboot_tag_basic_meminfo*)tag;
                boot_info.mem_lower = m->mem_lower;
                boot_info.mem_upper = m->mem_upper;
                break;
            }

            case MULTIBOOT2_TAG_MODULE: {
                struct multiboot_tag_module* m = (struct multiboot_tag_module*)tag;
                if (boot_info.module_count < BOOT_MODULE_MAX) {
                    struct boot_module* mod = &boot_info.modules[boot_info.module_count++];
                    mod->start = m->mod_start;
                    mod->end = m->mod_end;
                    multiboot2_copy_str(mod->cmdline, sizeof(mod->cmdline), m->string, tag->size);
                }
                break;
            }

            case MULTIBOOT2_TAG_MMAP: {
                struct multiboot_tag_mmap* mm = (struct multiboot_tag_mmap*)tag;
                if (mm->entry_size < sizeof(struct multiboot_mmap_entry)) break;

                uint32_t count = (mm->size - 16) / mm->entry_size;
                uint8_t* p = (uint8_t*)mm->entries;

                for (uint32_t i = 0; i < count; i++) {
                    struct multiboot_mmap_entry* e = (struct multiboot_mmap_entry*)(p + i * mm->entry_size);

                    if (e->len == 0) continue;
                    if (boot_info.mmap_count >= BOOT_MEMMAP_MAX) break;

                    struct boot_memmap_entry* out = &boot_info.mmap[boot_info.mmap_count++];
                    out->base = e->addr;
                    out->length = e->len;
                    out->type = multiboot2_mem_type(e->type);
                }
                break;
            }

            case MULTIBOOT2_TAG_EFI_MMAP: {
                /* На UEFI с живыми boot services нормальной карты памяти
                 * GRUB не отдает, вместо нее приходит карта в формате EFI. */
                struct multiboot_tag_efi_mmap* em = (struct multiboot_tag_efi_mmap*)tag;
                if (em->descr_size < sizeof(struct multiboot_efi_memory_desc)) break;
                if (boot_info.mmap_count != 0) break;    /* обычная карта главнее */

                uint32_t count = (em->size - 16) / em->descr_size;
                uint8_t* p = em->efi_mmap;

                for (uint32_t i = 0; i < count; i++) {
                    struct multiboot_efi_memory_desc* e =
                        (struct multiboot_efi_memory_desc*)(p + i * em->descr_size);

                    if (e->num_pages == 0) continue;
                    if (boot_info.mmap_count >= BOOT_MEMMAP_MAX) break;

                    struct boot_memmap_entry* out = &boot_info.mmap[boot_info.mmap_count++];
                    out->base = e->physical_start;
                    out->length = e->num_pages * 4096;
                    out->type = multiboot2_efi_mem_type(e->type);
                }
                break;
            }

            /* Тег фреймбуфера сознательно не разбирается: фреймбуфер
             * ядро получает у VBE или GOP, а не у загрузчика. */
            case MULTIBOOT2_TAG_FRAMEBUFFER:
            case MULTIBOOT2_TAG_VBE:
                break;

            case MULTIBOOT2_TAG_EFI32: {
                struct multiboot_tag_efi32* e = (struct multiboot_tag_efi32*)tag;
                boot_info.efi_system_table = e->pointer;
                boot_info.firmware = BOOT_FIRMWARE_EFI;
                break;
            }

            case MULTIBOOT2_TAG_EFI64: {
                struct multiboot_tag_efi64* e = (struct multiboot_tag_efi64*)tag;
                boot_info.efi_system_table = e->pointer;
                boot_info.firmware = BOOT_FIRMWARE_EFI;
                break;
            }

            case MULTIBOOT2_TAG_EFI_BS:
                efi_bs_seen = 1;
                break;

            case MULTIBOOT2_TAG_LOAD_BASE_ADDR: {
                struct multiboot_tag_load_base_addr* l = (struct multiboot_tag_load_base_addr*)tag;
                /* полезно только для диагностики, ядро само знает свою базу */
                (void)l;
                break;
            }

            default:
                break;
        }

        tag = (struct multiboot_tag*)((uint8_t*)tag + ((tag->size + 7) & ~7));
    }

    boot_info.efi_boot_services = efi_bs_seen;

    if (boot_info.mmap_count > 0) {
        boot_memmap_summary();
    } else if (boot_info.mem_lower == 0 && boot_info.mem_upper == 0) {
        /* старый GRUB: только basic_meminfo, карты памяти нет */
        boot_info.mem_lower = 640;
        boot_info.mem_upper = 0;
    }
}
