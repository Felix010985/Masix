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

#define PT_LOAD 1


static int elf_header_valid(const Elf32_Ehdr* header, uint32_t file_size) {
    if (header->e_ident[0] != 0x7F || header->e_ident[1] != 'E' ||
        header->e_ident[2] != 'L' || header->e_ident[3] != 'F') return 0;
    if (header->e_phentsize != sizeof(Elf32_Phdr)) return 0;
    if (header->e_phoff > file_size ||
        header->e_phnum > (file_size - header->e_phoff) / sizeof(Elf32_Phdr)) return 0;
    return 1;
}

void* elf_load_binary(uint32_t file_start) {
    Elf32_Ehdr* elf_header = (Elf32_Ehdr*)file_start;

    if (!elf_header_valid(elf_header, 0xFFFFFFFFU)) {
        debug_puts("Masix: ELF: Invalid magic signature!\n");
        return NULL;
    }

    /* Создаем новое независимое пространство виртуальной памяти для процесса */
    if (vmm_userspace_create() != 0) {
        debug_puts("Masix: ELF: Failed to create user space page directory!\n");
        return NULL;
    }

    Elf32_Phdr* phdr = (Elf32_Phdr*)(file_start + elf_header->e_phoff);

    for (int i = 0; i < elf_header->e_phnum; i++) {
        if (phdr[i].p_type == PT_LOAD) {

            uint32_t start_vaddr = phdr[i].p_vaddr;
            uint32_t end_vaddr = start_vaddr + phdr[i].p_memsz;

            uint32_t page_start = start_vaddr & ~0xFFF;
            uint32_t page_end = (end_vaddr + 0xFFF) & ~0xFFF;

            /* Идем по страницам, выделяем память и мапим её */
            for (uint32_t vaddr = page_start; vaddr < page_end; vaddr += PAGE_SIZE) {
                uint32_t phys = pmm_alloc_zeroed();
                if (!phys) {
                    debug_puts("Masix: ELF: Out of physical memory!\n");
                    return NULL;
                }
                // Мапим виртуальный адрес на выделенную физическую страницу
                // Флаг PTE_W берем, если сегмент ELF разрешает запись.
                uint32_t flags = (phdr[i].p_flags & 0x2) ? PAGE_WRITE : 0;
                vmm_user_map(vaddr, phys, flags);
            }

            /* Копируем данные из initramfs в только что замапленную виртуальную память */
            if (phdr[i].p_filesz > 0) {
                memcpy((void*)start_vaddr, (void*)(file_start + phdr[i].p_offset), phdr[i].p_filesz);
            }

            /* Если размер в памяти больше размера в файле, обнуляем остаток */
            if (phdr[i].p_memsz > phdr[i].p_filesz) {
                uint32_t bss_start = start_vaddr + phdr[i].p_filesz;
                uint32_t bss_size = phdr[i].p_memsz - phdr[i].p_filesz;
                memset((void*)bss_start, 0, bss_size);
            }
        }
    }

    /* Загружаем свежесозданный каталог страниц в регистр CR3 процессора,
     * чтобы активировать изолированное адресное пространство этой программы.
     */
    vmm_user_switch();
    uint32_t entry = elf_header->e_entry;
    __asm__ volatile("" : : "a"(entry));

    return (void*)entry;
    //return (void*)elf_header->e_entry;
}

void* elf_load_node(vfs_node_t* node) {
    Elf32_Ehdr header;
    if (!node || !node->read || node->read(node, 0, sizeof(header), (uint8_t*)&header) != sizeof(header) ||
        !elf_header_valid(&header, node->size)) {
        return NULL;
    }

    if (vmm_userspace_create() != 0) return NULL;

    for (uint16_t i = 0; i < header.e_phnum; i++) {
        Elf32_Phdr phdr;
        uint32_t offset = header.e_phoff + (uint32_t)i * sizeof(phdr);
        if (node->read(node, offset, sizeof(phdr), (uint8_t*)&phdr) != sizeof(phdr)) goto fail;
        if (phdr.p_type != PT_LOAD) continue;
        if (phdr.p_memsz < phdr.p_filesz ||
            phdr.p_offset > node->size || phdr.p_filesz > node->size - phdr.p_offset ||
            phdr.p_vaddr >= USER_TOP || phdr.p_memsz > USER_TOP - phdr.p_vaddr) { debug_puts("Masix: ELF: bad phdr bounds\n"); goto fail; }

        uint32_t page_start = phdr.p_vaddr & ~(PAGE_SIZE - 1);
        uint32_t page_end = (phdr.p_vaddr + phdr.p_memsz + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
        for (uint32_t vaddr = page_start; vaddr < page_end; vaddr += PAGE_SIZE) {
            uint32_t phys = pmm_alloc_zeroed();
            if (!phys || vmm_user_map(vaddr, phys, (phdr.p_flags & 2) ? PAGE_WRITE : 0) != 0) goto fail;
        }

        if (phdr.p_filesz && node->read(node, phdr.p_offset, phdr.p_filesz,
                                        (uint8_t*)phdr.p_vaddr) != (int32_t)phdr.p_filesz) goto fail;
        if (phdr.p_memsz > phdr.p_filesz) {
            memset((void*)(phdr.p_vaddr + phdr.p_filesz), 0, phdr.p_memsz - phdr.p_filesz);
        }
    }

    vmm_user_switch();
    return (void*)header.e_entry;

fail:
    return NULL;
}
