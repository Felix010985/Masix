/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef FS_H
#define FS_H

#include <stdint.h>

#define DISK_SIZE 1024*1024
#define BLOCK_SIZE 512
#define MAX_FILES 1024
#define MAX_BLOCKS_PER_FILE 10
#define TOTAL_BLOCKS (DISK_SIZE / BLOCK_SIZE)
#define FS_REGULAR 1
#define FS_DIRECTORY 2

extern unsigned char disk[DISK_SIZE];
extern unsigned char block_bitmap[TOTAL_BLOCKS/8];

int is_block_free(int index);
void set_block_used(int index);
void set_block_free(int index);
int allocate_block(void);

typedef struct {
    char name[256];
    unsigned int size;
    int blocks[MAX_BLOCKS_PER_FILE];
    unsigned int type;
    const unsigned char *external_data;
    int used;
} FileEntry;

extern FileEntry file_table[MAX_FILES];

// операции с файлами
int fs_create(const char* name);
int fs_create_type(const char* name, unsigned int type);
void fs_attach_external(int index, const unsigned char *data, unsigned int size);
void fs_delete(int index);
int fs_write(int file_index, const unsigned char* data, unsigned int size);
int fs_read(int file_index, unsigned char* buffer, unsigned int size);

#endif
// честно я спросил chatgpt зачем надо первые 2 строки и он сказал для безопасности кода, оставлю так пока :)
// это было 8 месяцев назад...
