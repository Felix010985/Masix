/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#include <vfs.h>
#include <string.h>
#include <config.h>

enum {
    DEV_NULL,
    DEV_ZERO,
    DEV_TTY,
    DEV_CONSOLE,
    DEV_FULL,
    DEV_RANDOM,
    DEV_COM1
};

static int32_t devtmpfs_null_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return 0;
}

static int32_t devtmpfs_null_write(vfs_node_t *node, uint32_t offset, uint32_t size, const uint8_t *buffer) {
    (void)node; (void)offset; (void)buffer;
    return size;
}

static int32_t devtmpfs_zero_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    for (uint32_t i = 0; i < size; i++) buffer[i] = 0;
    return size;
}

static int32_t devtmpfs_full_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    return devtmpfs_zero_read(node, offset, size, buffer);
}

static int32_t devtmpfs_full_write(vfs_node_t *node, uint32_t offset, uint32_t size, const uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return -28;
}

static uint32_t random_state = 0x4D617A75;

static int32_t devtmpfs_random_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    for (uint32_t i = 0; i < size; i++) {
        random_state = random_state * 1664525U + 1013904223U;
        buffer[i] = (uint8_t)(random_state >> 24);
    }
    return size;
}

static int32_t devtmpfs_com1_write(vfs_node_t *node, uint32_t offset, uint32_t size, const uint8_t *buffer) {
    (void)node; (void)offset;
    /* Fix: serial_puts убран и заменен на debug_puts по канонам масекса */
    for (uint32_t i = 0; i < size; i++) {
        char tmp[2] = { (char)buffer[i], 0 };
        debug_puts(tmp);
    }
    return size;
}

int32_t devtmpfs_open(const char *subpath, vfs_node_t *node) {
    node->flags = VFS_FILE;
    node->size = 0;
    node->read = devtmpfs_null_read;
    node->write = devtmpfs_null_write;
    if (strcmp(subpath, "null") == 0) { strcpy(node->name, "null"); node->internal_id = DEV_NULL; return 0; }
    if (strcmp(subpath, "zero") == 0) { strcpy(node->name, "zero"); node->internal_id = DEV_ZERO; node->read = devtmpfs_zero_read; return 0; }
    if (strcmp(subpath, "full") == 0) { strcpy(node->name, "full"); node->internal_id = DEV_FULL; node->read = devtmpfs_full_read; node->write = devtmpfs_full_write; return 0; }
    if (strcmp(subpath, "random") == 0) { strcpy(node->name, "random"); node->internal_id = DEV_RANDOM; node->read = devtmpfs_random_read; return 0; }
    if (strcmp(subpath, "com1") == 0) {
        strcpy(node->name, "com1"); node->internal_id = DEV_COM1;
        node->read = devtmpfs_null_read;
        node->write = devtmpfs_com1_write;
        return 0;
    }
    /* Это не наши устройства: их поставляют другие драйверы (tty, console, ...).
     * Мы только подключаем их зарегистрированные операции, кто их реализует
     * нам неизвестно. */
    const vfs_device_t *dev = vfs_lookup_device(subpath);
    if (dev) {
        strncpy(node->name, dev->name, sizeof(node->name) - 1);
        node->name[sizeof(node->name) - 1] = '\0';
        node->internal_id = -1;
        node->read = dev->read;
        node->write = dev->write;
        return 0;
    }
    return -2;
}

fsdriver_t devtmpfs_driver = {
    .open = devtmpfs_open
};
