/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#include <filesystem.h>

#define DISK_SIZE 1024*1024
#define BLOCK_SIZE 512
#define MAX_BLOCKS_PER_FILE 10
#define TOTAL_BLOCKS (DISK_SIZE / BLOCK_SIZE)

unsigned char disk[DISK_SIZE];
unsigned char block_bitmap[TOTAL_BLOCKS/8];

int fs_is_block_free(int index) {
    return !(block_bitmap[index / 8] & (1 << (index % 8)));
}

void fs_set_block_used(int index) {
    block_bitmap[index / 8] |= (1 << (index % 8));
}

void fs_set_block_free(int index) {
    block_bitmap[index / 8] &= ~(1 << (index % 8));
}

int fs_allocate_block() {
    for (int i = 0; i < TOTAL_BLOCKS; i++) {
        if (fs_is_block_free(i)) {
            fs_set_block_used(i);
            return i;
        }
    }
    return -1;
}

FileEntry file_table[MAX_FILES];

int fs_create(const char* name) {
    return fs_create_type(name, FS_REGULAR);
}

int fs_create_type(const char* name, unsigned int type) {
    if (!name || name[0] == 0) return -1;
    if (type != FS_REGULAR && type != FS_DIRECTORY) return -1;
    for (unsigned l = 0; name[l]; l++) { if (l > 254) return -1; }
    for (int i = 0; i < MAX_FILES; i++) {
        if (!file_table[i].used) {
            file_table[i].used = 1;
            file_table[i].size = 0;
            file_table[i].type = type;
            file_table[i].external_data = 0;
            for (int j = 0; j < MAX_BLOCKS_PER_FILE; j++)
                file_table[i].blocks[j] = -1;
            for (int k = 0; k < 255 && name[k]; k++)
                file_table[i].name[k] = name[k];
            file_table[i].name[255] = 0;
            return i;
        }
    }
    return -1;
}

void fs_attach_external(int index, const unsigned char *data, unsigned int size) {
    if (index < 0 || index >= MAX_FILES || !file_table[index].used) return;
    file_table[index].external_data = data;
    file_table[index].size = size;
}

void fs_delete(int index) {
    if (index < 0 || index >= MAX_FILES || !file_table[index].used) return;
    for (int i = 0; i < MAX_BLOCKS_PER_FILE; i++) {
        if (file_table[index].blocks[i] != -1)
            fs_set_block_free(file_table[index].blocks[i]);
    }
    file_table[index].used = 0;
}

int fs_write(int file_index, const unsigned char* data, unsigned int size) {
    if (file_index < 0 || file_index >= MAX_FILES || !file_table[file_index].used) return -22;
    if (!data && size) return -14;
    FileEntry* f = &file_table[file_index];
    if (f->external_data) return -30;
    unsigned int written = 0;
    for (int i = 0; i < MAX_BLOCKS_PER_FILE && written < size; i++) {
        if (f->blocks[i] == -1) {
            int block = fs_allocate_block();
            if (block == -1) return written;
            f->blocks[i] = block;
        }
        unsigned int to_write = (size - written) < BLOCK_SIZE ? (size - written) : BLOCK_SIZE;
        for (unsigned int j = 0; j < to_write; j++)
            disk[f->blocks[i] * BLOCK_SIZE + j] = data[written + j];
        written += to_write;
    }
    f->size = written;
    return written;
}

int fs_read_at(int file_index, unsigned int offset, unsigned char* buffer, unsigned int size) {
    if (file_index < 0 || file_index >= MAX_FILES || !file_table[file_index].used) return -22;
    if (!buffer && size) return -14;
    FileEntry* f = &file_table[file_index];
    if (offset >= f->size) return 0;
    if (size > f->size - offset) size = f->size - offset;

    if (f->external_data) {
        for (unsigned int i = 0; i < size; i++)
            buffer[i] = f->external_data[offset + i];
        return size;
    }

    unsigned int read = 0;
    while (read < size) {
        unsigned int position = offset + read;
        unsigned int block = position / BLOCK_SIZE;
        unsigned int block_offset = position % BLOCK_SIZE;
        if (block >= MAX_BLOCKS_PER_FILE || f->blocks[block] == -1) break;

        unsigned int available = BLOCK_SIZE - block_offset;
        unsigned int to_read = (size - read) < available ? (size - read) : available;
        for (unsigned int j = 0; j < to_read; j++) {
            buffer[read + j] = disk[f->blocks[block] * BLOCK_SIZE + block_offset + j];
        }
        read += to_read;
    }
    return read;
}

int fs_read(int file_index, unsigned char* buffer, unsigned int size) {
    return fs_read_at(file_index, 0, buffer, size);
}
