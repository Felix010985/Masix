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

int32_t sys_read(int fd, char *buf, uint32_t count);
int32_t sys_write(int fd, const char *buf, uint32_t count);
int32_t sys_chdir(const char *path);
int32_t sys_mount(const char *source, const char *target, const char *filesystemtype, unsigned long flags, const void *data);
int32_t sys_dup2(int32_t oldfd, int32_t newfd);
int32_t sys_wait4(int pid, int *wstatus, int options, void *rusage);
int32_t sys_fstat(int fd, struct kstat *st);
int32_t sys_fstat64(int fd, struct kstat *st);
int32_t sys_fstatat(int fd, const char *path, struct kstat *st, int flags);
int32_t sys_ioctl(int fd, uint32_t request, void *argp);
int32_t sys_getdents64(int fd, void *dirp, uint32_t count);
int32_t sys_nanosleep(const void *req, void *rem);
int32_t sys_clock_gettime32(int clk, void *ts);
int32_t sys_clock_gettime64(int clk, void *ts);
int32_t sys_getrlimit(int resource, void *rlim);

#endif
int32_t sys_brk(uint32_t new_brk);
int32_t sys_mmap2(uint32_t addr, uint32_t length, uint32_t prot, uint32_t flags, int fd, uint32_t pgoffset);
