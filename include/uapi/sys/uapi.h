/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef UAPI_H
#define UAPI_H

#include <stdint.h>
#include <kstat.h>

/* Общие системные вызовы */
int32_t sys_read(int fd, char *buf, uint32_t count);
int32_t sys_write(int fd, const char *buf, uint32_t count);
int32_t sys_chdir(const char *path);
int32_t sys_mount(const char *source, const char *target, const char *filesystemtype, unsigned long flags, const void *data);
int32_t sys_dup2(int32_t oldfd, int32_t newfd);
int32_t sys_wait4(int pid, int *wstatus, int options, void *rusage);
int32_t sys_fstat(int fd, struct kstat *st);
int32_t sys_fstatat(int fd, const char *path, struct kstat *st, int flags);
int32_t sys_ioctl(int fd, uint32_t request, void *argp);
int32_t sys_nanosleep(const void *req, void *rem);
int32_t sys_brk(uint32_t new_brk);

/* Пробрасываем архитектурную специфику конкретного таргета */
#include <asm/uapi.h>

#endif /* UAPI_H */
