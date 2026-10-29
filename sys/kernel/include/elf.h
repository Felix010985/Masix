/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef ELF_H
#define ELF_H

#include <stdint.h>

#include <vfs.h>

/* Данные для auxv начального стека: их заполняет ELF-загрузчик, а
 * task_execve/task_create кладут в стек. Нужны динамическому линковщику
 * musl (AT_PHDR/AT_ENTRY/AT_BASE), чтобы он нашел программу, её
 * заголовки и собственную базу. */
struct elf_aux_info {
    uint32_t phdr;   /* AT_PHDR  - адрес таблицы phdr основной программы */
    uint32_t phent;  /* AT_PHENT - размер записи phdr */
    uint32_t phnum;  /* AT_PHNUM - число записей phdr */
    uint32_t entry;  /* AT_ENTRY - точка входа основной программы */
    uint32_t base;   /* AT_BASE  - база интерпретатора */
    uint32_t interp; /* точка входа интерпретатора (0, если его нет) */
};

extern struct elf_aux_info elf_aux;

void* elf_load_binary(uint32_t file_start);
void* elf_load_node(vfs_node_t* node);

#endif
