/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#define DISK_SIZE 1024*1024
#define BLOCK_SIZE 512
#define MAX_FILES 64
#define MAX_BLOCKS_PER_FILE 10
#define TOTAL_BLOCKS (DISK_SIZE / BLOCK_SIZE)

unsigned char disk[DISK_SIZE];
unsigned char block_bitmap[TOTAL_BLOCKS/8];

int is_block_free(int index) {
    return !(block_bitmap[index / 8] & (1 << (index % 8)));
}

void set_block_used(int index) {
    block_bitmap[index / 8] |= (1 << (index % 8));
}

void set_block_free(int index) {
    block_bitmap[index / 8] &= ~(1 << (index % 8));
}

int allocate_block() {
    for (int i = 0; i < TOTAL_BLOCKS; i++) {
        if (is_block_free(i)) {
            set_block_used(i);
            return i;
        }
    }
    return -1;
}

typedef struct {
    char name[32];
    unsigned int size;
    int blocks[MAX_BLOCKS_PER_FILE];
    int used;
} FileEntry;

FileEntry file_table[MAX_FILES];

int fs_create(const char* name) {
    for (int i = 0; i < MAX_FILES; i++) {
        if (!file_table[i].used) {
            file_table[i].used = 1;
            file_table[i].size = 0;
            for (int j = 0; j < MAX_BLOCKS_PER_FILE; j++)
                file_table[i].blocks[j] = -1;
            for (int k = 0; k < 31 && name[k]; k++)
                file_table[i].name[k] = name[k];
            file_table[i].name[31] = 0;
            return i;
        }
    }
    return -1;
}

void fs_delete(int index) {
    if (!file_table[index].used) return;
    for (int i = 0; i < MAX_BLOCKS_PER_FILE; i++) {
        if (file_table[index].blocks[i] != -1)
            set_block_free(file_table[index].blocks[i]);
    }
    file_table[index].used = 0;
}

int fs_write(int file_index, const unsigned char* data, unsigned int size) {
    FileEntry* f = &file_table[file_index];
    unsigned int written = 0;
    for (int i = 0; i < MAX_BLOCKS_PER_FILE && written < size; i++) {
        if (f->blocks[i] == -1) {
            int block = allocate_block();
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
    FileEntry* f = &file_table[file_index];
    if (offset >= f->size) return 0;
    if (size > f->size - offset) size = f->size - offset;

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
