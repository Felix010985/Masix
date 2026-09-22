/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef API_H
#define API_H

#include <stdint.h>
#include <stddef.h>

#define MAX_INPUT 128

void print(const char* str, uint8_t color);
void cls(void);
void read_line(char* buffer, uint8_t color);
int32_t sys_chdir(const char *path);
void sys_print(const char* str, uint8_t color);
void sys_cls(void);
void sys_read_line(char* buffer, uint8_t color);
int32_t sys_mount(const char *source, const char *target, const char *filesystemtype, unsigned long flags, const void *data);
int32_t sys_dup2(int32_t oldfd, int32_t newfd);
int32_t k_sys_write(int fd, const char *buf, uint32_t count);
int32_t k_sys_read(int fd, char *buf, uint32_t count);

int strcmp(const char* a, const char* b);

void* malloc(size_t size);

void free(void* ptr);

#endif
