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
#include <config.h>
#include <vmm.h>
#include <pmm.h>
#include <vfs.h>
#include <elf.h>
#include <string.h>
#include <stdio.h>

typedef struct {
    unsigned char e_ident[16];
    uint16_t      e_type;
    uint16_t      e_machine;
    uint32_t      e_version;
    uint32_t      e_entry;
    uint32_t      e_phoff;
    uint32_t      e_shoff;
    uint32_t      e_flags;
    uint16_t      e_ehsize;
    uint16_t      e_phentsize;
    uint16_t      e_phnum;
    uint16_t      e_shentsize;
    uint16_t      e_shnum;
    uint16_t      e_shstrndx;
} Elf32_Ehdr;

typedef struct {
    uint32_t p_type;
    uint32_t p_offset;
    uint32_t p_vaddr;
    uint32_t p_paddr;
    uint32_t p_filesz;
    uint32_t p_memsz;
    uint32_t p_flags;
    uint32_t p_align;
} Elf32_Phdr;

#define PT_LOAD   1
#define PT_INTERP 3

#define ELF_ET_DYN 3

/* Базы для образов, собранных как PIE (ET_DYN). Сегменты таких файлов
 * описаны относительно нуля, поэтому загрузчику нужна ненулевая база:
 * иначе первый же PT_LOAD отобразился бы на страницу 0. Основную
 * программу и интерпретатор раскладываем в разные регионы ниже USER_TOP. */
#define ELF_MAIN_BASE   0x08048000u
#define ELF_INTERP_BASE 0x40000000u

struct elf_aux_info elf_aux;

/* Источник байтов ELF: либо сырой образ в памяти (init из initramfs),
 * либо файл VFS (когда программу поднимает execve). */
struct elf_source {
    const uint8_t* mem;
    uint32_t       mem_size;
    vfs_node_t*    node;
};

static int32_t elf_source_read(void* context, uint32_t offset, uint32_t size, void* buffer) {
    struct elf_source* src = (struct elf_source*)context;
    if (src->mem) {
        if (offset > src->mem_size || size > src->mem_size - offset) return -1;
        memcpy(buffer, src->mem + offset, size);
        return (int32_t)size;
    }
    if (src->node && src->node->read) {
        return src->node->read(src->node, offset, size, (uint8_t*)buffer);
    }
    return -1;
}

static int elf_header_valid(const Elf32_Ehdr* header, uint32_t file_size) {
    if (header->e_ident[0] != 0x7F || header->e_ident[1] != 'E' ||
        header->e_ident[2] != 'L' || header->e_ident[3] != 'F') return 0;
    if (header->e_phentsize != sizeof(Elf32_Phdr)) return 0;
    if (header->e_phoff > file_size ||
        header->e_phnum > (file_size - header->e_phoff) / sizeof(Elf32_Phdr)) return 0;
    return 1;
}

/* Раскладывает PT_LOAD-сегменты одного DSO по его базе. Для ET_EXEC база
 * игнорируется: адреса в сегментах уже абсолютные. Возвращает точку входа
 * с учетом базы либо 0 при ошибке. Через out_phdr отдает виртуальный адрес
 * таблицы программных заголовков (это нужно musl-овскому AT_PHDR). */
static uint32_t elf_map_dso(struct elf_source* src, const Elf32_Ehdr* eh,
                            uint32_t base, uint32_t* out_phdr) {
    uint32_t bias = (eh->e_type == ELF_ET_DYN) ? base : 0;

    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        Elf32_Phdr ph;
        if (elf_source_read(src, eh->e_phoff + (uint32_t)i * sizeof(Elf32_Phdr),
                            sizeof(ph), &ph) != (int32_t)sizeof(ph)) {
            debug_puts("Masix: ELF: phdr read failed\n");
            return 0;
        }
        if (ph.p_type != PT_LOAD) continue;

        uint32_t vstart = ph.p_vaddr + bias;
        uint32_t vend = vstart + ph.p_memsz;
        uint32_t page_start = vstart & ~(PAGE_SIZE - 1);
        uint32_t page_end = (vend + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);

        /* Идем по страницам, выделяем память и мапим её. Флаг PTE_W берем,
         * если сегмент ELF разрешает запись. */
        for (uint32_t va = page_start; va < page_end; va += PAGE_SIZE) {
            uint32_t phys = pmm_alloc_zeroed();
            if (!phys) {
                debug_puts("Masix: ELF: Out of physical memory!\n");
                return 0;
            }
            uint32_t flags = (ph.p_flags & 0x2) ? PAGE_WRITE : 0;
            if (vmm_user_map(va, phys, flags) != 0) return 0;
        }

        /* Копируем данные из источника в замапленную виртуальную память.
         * Ring 0 пишет и в read-only страницы: CR0.WP ядро не включает. */
        if (ph.p_filesz > 0) {
            if (elf_source_read(src, ph.p_offset, ph.p_filesz,
                                (void*)vstart) != (int32_t)ph.p_filesz) {
                debug_puts("Masix: ELF: phdr data read failed\n");
                return 0;
            }
        }

        /* Если размер в памяти больше размера в файле, обнуляем остаток. */
        if (ph.p_memsz > ph.p_filesz) {
            memset((void*)(vstart + ph.p_filesz), 0, ph.p_memsz - ph.p_filesz);
        }
    }

    if (out_phdr) *out_phdr = bias + eh->e_phoff;
    return bias + eh->e_entry;
}

/* Ищет PT_INTERP и копирует путь интерпретатора в out (с завершающим 0). */
static int elf_find_interp(struct elf_source* src, const Elf32_Ehdr* eh,
                           char* out, uint32_t out_size) {
    for (uint16_t i = 0; i < eh->e_phnum; i++) {
        Elf32_Phdr ph;
        if (elf_source_read(src, eh->e_phoff + (uint32_t)i * sizeof(Elf32_Phdr),
                            sizeof(ph), &ph) != (int32_t)sizeof(ph)) return -1;
        if (ph.p_type != PT_INTERP) continue;
        if (ph.p_filesz == 0 || ph.p_filesz >= out_size) return -1;
        if (elf_source_read(src, ph.p_offset, ph.p_filesz, out) != (int32_t)ph.p_filesz) return -1;
        out[ph.p_filesz] = '\0';
        return 0;
    }
    return -1;
}

/* Общая загрузка ELF: создает адресное пространство, раскладывает основную
 * программу и, если файл динамический, интерпретатор (musl ld.so). Возвращает
 * точку входа, на которую нужно прыгнуть (для динамических программ - это
 * точка входа интерпретатора), и заполняет elf_aux для начального стека. */
static void* elf_load_common(struct elf_source* main_src) {
    Elf32_Ehdr eh;
    if (elf_source_read(main_src, 0, sizeof(eh), &eh) != (int32_t)sizeof(eh) ||
        !elf_header_valid(&eh, main_src->mem ? main_src->mem_size : main_src->node->size)) {
        debug_puts("Masix: ELF: Invalid magic signature!\n");
        return NULL;
    }

    /* Создаем новое независимое пространство виртуальной памяти для процесса */
    if (vmm_userspace_create() != 0) {
        debug_puts("Masix: ELF: Failed to create user space page directory!\n");
        return NULL;
    }

    memset(&elf_aux, 0, sizeof(elf_aux));
    elf_aux.phent = sizeof(Elf32_Phdr);
    elf_aux.phnum = eh.e_phnum;

    uint32_t main_entry = elf_map_dso(main_src, &eh, ELF_MAIN_BASE, &elf_aux.phdr);
    if (!main_entry) return NULL;
    elf_aux.entry = main_entry;

    char interp[256];
    if (elf_find_interp(main_src, &eh, interp, sizeof(interp)) == 0) {
        debug_puts("Masix: ELF: interpreter: ");
        debug_puts(interp);
        debug_puts("\n");

        int fd = vfs_open(interp);
        if (fd < 0) {
            debug_puts("Masix: ELF: interpreter not found!\n");
            return NULL;
        }

        struct elf_source isrc;
        isrc.mem = NULL;
        isrc.mem_size = 0;
        isrc.node = (vfs_node_t*)vfs_fd_table()[fd].private_data;

        Elf32_Ehdr ieh;
        uint32_t interp_entry = 0;
        int32_t hdr_read = elf_source_read(&isrc, 0, sizeof(ieh), &ieh);
        int hdr_valid = 0;
        if (hdr_read == (int32_t)sizeof(ieh)) {
            hdr_valid = elf_header_valid(&ieh, isrc.node->size);
        }
        if (hdr_read == (int32_t)sizeof(ieh) && hdr_valid) {
            interp_entry = elf_map_dso(&isrc, &ieh, ELF_INTERP_BASE, NULL);
        }
        vfs_close(fd);

        if (!interp_entry) {
            debug_puts("Masix: ELF: failed to load interpreter!\n");
            return NULL;
        }

        elf_aux.base = ELF_INTERP_BASE;
        elf_aux.interp = interp_entry;

        vmm_user_switch();
        return (void*)interp_entry;
    }

    /* Статический файл: прыгаем прямо на него, интерпретатор не нужен. */
    vmm_user_switch();
    return (void*)main_entry;
}

void* elf_load_binary(uint32_t file_start) {
    struct elf_source src;
    src.mem = (const uint8_t*)file_start;
    src.mem_size = 0xFFFFFFFFU;
    src.node = NULL;
    return elf_load_common(&src);
}

void* elf_load_node(vfs_node_t* node) {
    if (!node || !node->read) return NULL;
    struct elf_source src;
    src.mem = NULL;
    src.mem_size = 0;
    src.node = node;
    return elf_load_common(&src);
}
