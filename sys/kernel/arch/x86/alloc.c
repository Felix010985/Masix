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
#include <alloc.h>

static uint8_t heap[HEAP_SIZE];

/* Каждому выделенному блоку памяти предшествует этот заголовок */
typedef struct block_header {
    size_t size;
    int is_free;
    struct block_header *next;
} block_header_t;

static block_header_t *free_list = (block_header_t*)heap;
static int is_initialized = 0;

/* Системный вызов исполняется с разрешёнными прерываниями, поэтому тик
 * PIT может вытеснить задачу прямо посреди работы с кучей, а следующая
 * задача - снова позвать malloc. Список свободных блоков от такого
 * наложения рушится, и два выделения возвращают одну и ту же память.
 * Поэтому на время операций над кучей запрещаем прерывания. */
static inline uint32_t alloc_irq_save(void) {
    uint32_t flags;
    __asm__ __volatile__("pushfl\n\tpopl %0\n\tcli" : "=r"(flags) :: "memory");
    return flags;
}

static inline void alloc_irq_restore(uint32_t flags) {
    __asm__ __volatile__("pushl %0\n\tpopfl" :: "r"(flags) : "memory");
}

void* alloc(size_t size) {
    uint32_t flags = alloc_irq_save();

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
            alloc_irq_restore(flags);
            return (void*)((uint8_t*)curr + sizeof(block_header_t));
        }
        curr = curr->next;
    }
    alloc_irq_restore(flags);
    return NULL;
}

void alloc_free(void* ptr) {
    if (!ptr) return;

    uint32_t flags = alloc_irq_save();

    /* Освобождать можно только указатель внутри кучи: узлы устройств
     * из BSS (например console_node) не наши. */
    if ((uint8_t*)ptr < heap + sizeof(block_header_t) ||
        (uint8_t*)ptr >= heap + HEAP_SIZE) {
        alloc_irq_restore(flags);
        return;
    }

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

    alloc_irq_restore(flags);
}

/* Удобные функции снизу */

void* malloc(size_t size) {
    return alloc(size);
}

void free(void* ptr) {
    alloc_free(ptr);
}
