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

enum {
    DEV_NULL,
    DEV_ZERO,
    DEV_TTY,
    DEV_CONSOLE,
    DEV_FULL,
    DEV_RANDOM
};

static int32_t dev_null_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return 0;
}

static int32_t dev_null_write(vfs_node_t *node, uint32_t offset, uint32_t size, const uint8_t *buffer) {
    (void)node; (void)offset; (void)buffer;
    return size;
}

static int32_t dev_zero_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    for (uint32_t i = 0; i < size; i++) buffer[i] = 0;
    return size;
}

static int32_t dev_full_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    return dev_zero_read(node, offset, size, buffer);
}

static int32_t dev_full_write(vfs_node_t *node, uint32_t offset, uint32_t size, const uint8_t *buffer) {
    (void)node; (void)offset; (void)size; (void)buffer;
    return -28;
}

static int32_t dev_tty_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node;
    (void)offset;
    extern char keyboard_getc(void);
    extern void tty_write_char(char c);
    if (size == 0) return 0;

    uint32_t read = 0;
    while (read < size) {
        char c = keyboard_getc();
        if (c == '\r') c = '\n';
        buffer[read++] = (uint8_t)c;
        tty_write_char(c);
        if (c == '\n') break;
    }
    return read;
}

static uint32_t random_state = 0x4D617A75;

static int32_t dev_random_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    (void)node; (void)offset;
    for (uint32_t i = 0; i < size; i++) {
        random_state = random_state * 1664525U + 1013904223U;
        buffer[i] = (uint8_t)(random_state >> 24);
    }
    return size;
}

static int32_t dev_tty_write(vfs_node_t *node, uint32_t offset, uint32_t size, const uint8_t *buffer) {
    (void)node; (void)offset;
    extern void tty_write_char(char c);
    for (uint32_t i = 0; i < size; i++) tty_write_char((char)buffer[i]);
    return size;
}

int32_t devtmpfs_open(const char *subpath, vfs_node_t *node) {
    node->flags = VFS_FILE;
    node->size = 0;
    node->read = dev_null_read;
    node->write = dev_null_write;
    if (strcmp(subpath, "null") == 0) { strcpy(node->name, "null"); node->internal_id = DEV_NULL; return 0; }
    if (strcmp(subpath, "zero") == 0) { strcpy(node->name, "zero"); node->internal_id = DEV_ZERO; node->read = dev_zero_read; return 0; }
    if (strcmp(subpath, "tty") == 0) { strcpy(node->name, "tty"); node->internal_id = DEV_TTY; node->read = dev_tty_read; node->write = dev_tty_write; return 0; }
    if (strcmp(subpath, "console") == 0) { strcpy(node->name, "console"); node->internal_id = DEV_CONSOLE; node->read = dev_tty_read; node->write = dev_tty_write; return 0; }
    if (strcmp(subpath, "full") == 0) { strcpy(node->name, "full"); node->internal_id = DEV_FULL; node->read = dev_full_read; node->write = dev_full_write; return 0; }
    if (strcmp(subpath, "random") == 0) { strcpy(node->name, "random"); node->internal_id = DEV_RANDOM; node->read = dev_random_read; return 0; }
    return -2;
}

fsdriver_t devtmpfs_driver = {
    .open = devtmpfs_open
};
