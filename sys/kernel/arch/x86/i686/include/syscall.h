/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#pragma once

#include <stdint.h>

#define LINUX_ENOSYS 38

#define MASIX_EXIT            1
#define MASIX_FORK            2
#define MASIX_READ            3
#define MASIX_WRITE           4
#define MASIX_OPEN            5
#define MASIX_CLOSE           6
#define MASIX_EXECVE          11
#define MASIX_CHDIR           12
#define MASIX_GETPID          20
#define MASIX_MOUNT           21
#define MASIX_PIPE            42
#define MASIX_BRK             45
#define MASIX_KILL            37
#define MASIX_ACCESS          33
#define MASIX_IOCTL           54
#define MASIX_FCNTL           55
#define MASIX_SETPGID         57
#define MASIX_DUP2            63
#define MASIX_GETPPID         64
#define MASIX_SETSID          66
#define MASIX_REBOOT          88
#define MASIX_MUNMAP          91
#define MASIX_FCHMOD          94
#define MASIX_SOCKETCALL      102
#define MASIX_SETITIMER       104
#define MASIX_FSTAT           108
#define MASIX_READLINK        85
#define MASIX_READLINKAT      305
#define MASIX_UNAME           122
#define MASIX_FCHOWN32        207
#define MASIX_WAIT4           114
#define MASIX_MODIFY_LDT      123
#define MASIX_MPROTECT        125
#define MASIX_GETPGID         132
#define MASIX__LLSEEK         140
#define MASIX_GETDENTS        141
#define MASIX_GETSID          147
#define MASIX_WRITEV          146
#define MASIX_NANOSLEEP       162
#define MASIX_POLL            168
#define MASIX_RT_SIGACTION    174
#define MASIX_RT_SIGPROCMASK  175
#define MASIX_RT_SIGTIMEDWAIT 177
#define MASIX_GETCWD          183
#define MASIX_VFORK           190
#define MASIX_UGETRLIMIT      191
#define MASIX_MMAP2           192
#define MASIX_STAT64          195
#define MASIX_LSTAT64         196
#define MASIX_FCNTL64         221
#define MASIX_FSTAT64         197
#define MASIX_GETTID          224
#define MASIX_GETUID          24
#define MASIX_GETGID          47
#define MASIX_GETEUID         49
#define MASIX_GETEGID         50
#define MASIX_GETUID32        199
#define MASIX_GETGID32        200
#define MASIX_GETEUID32       201
#define MASIX_GETEGID32       202
#define MASIX_TKILL           238
#define MASIX_SET_THREAD_AREA 243
#define MASIX_EXIT_GROUP      252
#define MASIX_SET_TID_ADDRESS 258
#define MASIX_PSELECT6        308
#define MASIX_FACCESSAT       307
#define MASIX_PIPE2           331
#define MASIX_PRLIMIT64       340
#define MASIX_STATX           383
#define MASIX_CLOCK_GETTIME64 403

struct syscall_regs {
    uint32_t edi;
    uint32_t esi;
    uint32_t ebp;
    uint32_t esp;
    uint32_t ebx;
    uint32_t edx;
    uint32_t ecx;
    uint32_t eax;
};

uint32_t syscall_handler_c(struct syscall_regs *regs);
void syscall_init(void);

#define MASIX_GETRLIMIT            76
#define MASIX_CLOCK_GETTIME32      265
#define MASIX_CLOCK_NANOSLEEP_TIME32 267
#define MASIX_GETDENTS64           220
#define MASIX_FSTATAT64            300
#define MASIX_TGKILL               270

/* Прямые (не socketcall) сетевые вызовы i386. */
#define MASIX_SOCKET               359
#define MASIX_SOCKETPAIR           360
#define MASIX_BIND                 361
#define MASIX_CONNECT              362
#define MASIX_LISTEN               363
#define MASIX_ACCEPT4              364
#define MASIX_GETSOCKOPT           365
#define MASIX_SETSOCKOPT           366
#define MASIX_GETSOCKNAME          367
#define MASIX_GETPEERNAME          368
#define MASIX_SENDTO               369
#define MASIX_SENDMSG              370
#define MASIX_RECVFROM             371
#define MASIX_RECVMSG              372
#define MASIX_SHUTDOWN             373

#define AT_FDCWD                   (-100)
#define AT_SYMLINK_NOFOLLOW        0x100
#define AT_EMPTY_PATH              0x1000
#define WNOHANG                    1
#define ECHILD                     10
