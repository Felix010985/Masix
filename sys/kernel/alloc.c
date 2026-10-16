/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#include <stddef.h>
#include <stdint.h>

#define HEAP_SIZE 0x10000
static uint8_t heap[HEAP_SIZE];

/* Каждому выделенному блоку памяти предшествует этот заголовок */
typedef struct block_header {
    size_t size;
    int is_free;
    struct block_header *next;
} block_header_t;

static block_header_t *free_list = (block_header_t*)heap;
static int is_initialized = 0;

void* alloc(size_t size) {
    /* При первом вызове размечаем весь массив как один большой свободный блок */
    if (!is_initialized) {
        free_list->size = HEAP_SIZE - sizeof(block_header_t);
        free_list->is_free = 1;
        free_list->next = NULL;
        is_initialized = 1;
    }

    /* Выравниваем размер под 4 байта */
    size = (size + 3) & ~3;

    block_header_t *curr = free_list;
    while (curr) {
        if (curr->is_free && curr->size >= size) {
            /* Если блок сильно больше, отрезаем от него кусок */
            if (curr->size > size + sizeof(block_header_t) + 4) {
                block_header_t *next_block = (block_header_t*)((uint8_t*)curr + sizeof(block_header_t) + size);
                next_block->size = curr->size - size - sizeof(block_header_t);
                next_block->is_free = 1;
                next_block->next = curr->next;

                curr->size = size;
                curr->next = next_block;
            }
            curr->is_free = 0;
            return (void*)((uint8_t*)curr + sizeof(block_header_t));
        }
        curr = curr->next;
    }
    return NULL;
}

void alloc_free(void* ptr) {
    if (!ptr) return;

    block_header_t *header = (block_header_t*)((uint8_t*)ptr - sizeof(block_header_t));
    header->is_free = 1;

    /* Склеиваем соседние свободные блоки */
    block_header_t *curr = free_list;
    while (curr && curr->next) {
        if (curr->is_free && curr->next->is_free) {
            /* Поглощаем следующий блок и его заголовок */
            curr->size += sizeof(block_header_t) + curr->next->size;
            curr->next = curr->next->next;
            /* Не двигаем curr вперед, чтобы проверить, не свободен ли СЛЕДУЮЩИЙ за ним блок */
        } else {
            curr = curr->next;
        }
    }
}

void* malloc(size_t size) {
    return alloc(size);
}

void free(void* ptr) {
    alloc_free(ptr);
}
