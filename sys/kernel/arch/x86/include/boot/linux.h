/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef ARCH_X86_BOOT_LINUX_H
#define ARCH_X86_BOOT_LINUX_H

#include <stdint.h>

/* Протокол загрузки LINUX (bzImage). Заголовок setup лежит в первом
 * секторе образа со смещения 0x1f1, нулевая страница (boot_params)
 * приходит в ESI при 32-битном входе. Структуры общие для 32-бит и
 * 64-бит: все поля фиксированной ширины. */

#define LINUX_SETUP_MAGIC 0x53726448      /* "HdrS" */
#define LINUX_BOOT_FLAG   0xAA55

#define LINUX_LOADED_HIGH   0x01
#define LINUX_QUIET_FLAG    0x20
#define LINUX_CAN_USE_HEAP  0x80

#define LINUX_CL_MAGIC 0xA33F

/* Заголовок setup: смещения совпадают с протоколом x86 LINUX 2.10+ */
struct linux_setup_header {
    uint8_t  setup_sects;          /* 0x1f1 */
    uint16_t root_flags;           /* 0x1f2 */
    uint32_t syssize;              /* 0x1f4 */
    uint16_t ram_size;             /* 0x1f8 */
    uint16_t vid_mode;             /* 0x1fa */
    uint16_t root_dev;             /* 0x1fc */
    uint16_t boot_flag;            /* 0x1fe */
    uint16_t jump;                 /* 0x200 */
    uint32_t header;               /* 0x202 */
    uint16_t version;              /* 0x206 */
    uint32_t realmode_swtch;       /* 0x208 */
    uint16_t start_sys_seg;        /* 0x20c */
    uint16_t kernel_version;       /* 0x20e */
    uint8_t  type_of_loader;       /* 0x210 */
    uint8_t  loadflags;            /* 0x211 */
    uint16_t setup_move_size;      /* 0x212 */
    uint32_t code32_start;         /* 0x214 */
    uint32_t ramdisk_image;        /* 0x218 */
    uint32_t ramdisk_size;         /* 0x21c */
    uint32_t bootsect_kludge;      /* 0x220 */
    uint16_t heap_end_ptr;         /* 0x224 */
    uint8_t  ext_loader_ver;       /* 0x226 */
    uint8_t  ext_loader_type;      /* 0x227 */
    uint32_t cmd_line_ptr;         /* 0x228 */
    uint32_t initrd_addr_max;      /* 0x22c */
    uint32_t kernel_alignment;     /* 0x230 */
    uint8_t  relocatable_kernel;   /* 0x234 */
    uint8_t  min_alignment;        /* 0x235 */
    uint16_t xloadflags;           /* 0x236 */
    uint32_t cmdline_size;         /* 0x238 */
    uint32_t hardware_subarch;     /* 0x23c */
    uint64_t hardware_subarch_data;/* 0x240 */
    uint32_t payload_offset;       /* 0x248 */
    uint32_t payload_length;       /* 0x24c */
    uint64_t setup_data;           /* 0x250 */
    uint64_t pref_address;         /* 0x258 */
    uint32_t init_size;            /* 0x260 */
    uint32_t handover_offset;      /* 0x264 */
    uint32_t kernel_info_offset;   /* 0x268 */
} __attribute__((packed));

/* Сведения EFI, которые загрузчик кладет в нулевую страницу. */
struct linux_efi_info {
    uint32_t efi_loader_signature;
    uint32_t efi_systab;
    uint32_t efi_memdesc_size;
    uint32_t efi_memdesc_version;
    uint32_t efi_mmap;
    uint32_t efi_mmap_size;
    uint32_t efi_systab_hi;
    uint32_t efi_mmap_hi;
};

/* Запись карты памяти e820. В нулевой странице запись ровно 20 байт:
 * адрес 8, размер 8, тип 4. Никаких добивок, иначе следующая запись
 * уезжает на 4 байта и вся карта плывет. */
struct linux_e820_entry {
    uint64_t addr;
    uint64_t size;
    uint32_t type;
} __attribute__((packed));

/* Нулевая страница (boot_params). Объявлены только те поля,
 * которые читает ядро: все остальное закрыто выравниванием. */
struct linux_boot_params {
    uint8_t  pad0[0x1c0];                       /* 0x000: screen_info и прочее */
    struct linux_efi_info efi_info;             /* 0x1c0 */
    uint32_t alt_mem_k;                         /* 0x1e0 */
    uint32_t scratch;                           /* 0x1e4 */
    uint8_t  e820_entries;                      /* 0x1e8 */
    /* 0x1e9: eddbuf_entries, edd_mbr_sig_buf_entries, kbd_status,
     * secure_boot, pad5[2], sentinel, pad6[1], всего 8 байт до заголовка. */
    uint8_t  pad5[8];                           /* 0x1e9 .. 0x1f0 */
    struct linux_setup_header hdr;              /* 0x1f1 */
    uint8_t  pad6[0x2d0 - 0x1f1 - sizeof(struct linux_setup_header)];
    struct linux_e820_entry e820_table[128];    /* 0x2d0 */
} __attribute__((packed));

/* Типы участков e820, они совпадают с BOOT_MEMMAP_* в <boot.h>. */
#define LINUX_E820_RAM      1
#define LINUX_E820_RESERVED 2
#define LINUX_E820_ACPI     3
#define LINUX_E820_NVS      4
#define LINUX_E820_UNUSABLE 5

#endif /* ARCH_X86_BOOT_LINUX_H */
