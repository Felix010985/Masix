/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel - bootstrap userland
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * Прямые системные вызовы ядра Masix. Номера совпадают с
 * Linux i386 (и с sys/kernel/include/syscall.h): инт 0x80,
 * аргументы в ebx/ecx/edx/esi/edi, результат в eax.
 * Отрицательный результат - код ошибки (errno-подобный).
 */
#ifndef BOOTSTRAP_SYSCALL_H
#define BOOTSTRAP_SYSCALL_H

#define SYS_EXIT        1
#define SYS_FORK        2
#define SYS_READ        3
#define SYS_WRITE       4
#define SYS_OPEN        5
#define SYS_CLOSE       6
#define SYS_EXECVE      11
#define SYS_MOUNT       21
#define SYS_DUP2        63
#define SYS_WAIT4       114
#define SYS_EXIT_GROUP  252

/* Флаги open() (Linux O_*) */
#define O_RDONLY        0
#define O_RDWR          2

static inline long syscall0(long n) {
    long ret;
    __asm__ __volatile__("int $0x80" : "=a"(ret) : "a"(n));
    return ret;
}

static inline long syscall1(long n, long a) {
    long ret;
    __asm__ __volatile__("int $0x80" : "=a"(ret) : "a"(n), "b"(a));
    return ret;
}

static inline long syscall2(long n, long a, long b) {
    long ret;
    __asm__ __volatile__("int $0x80" : "=a"(ret) : "a"(n), "b"(a), "c"(b));
    return ret;
}

static inline long syscall3(long n, long a, long b, long c) {
    long ret;
    __asm__ __volatile__("int $0x80" : "=a"(ret) : "a"(n), "b"(a), "c"(b), "d"(c));
    return ret;
}

static inline long syscall4(long n, long a, long b, long c, long d) {
    long ret;
    __asm__ __volatile__("int $0x80"
                         : "=a"(ret)
                         : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d));
    return ret;
}

static inline long syscall5(long n, long a, long b, long c, long d, long e) {
    long ret;
    __asm__ __volatile__("int $0x80"
                         : "=a"(ret)
                         : "a"(n), "b"(a), "c"(b), "d"(c), "S"(d), "D"(e));
    return ret;
}

static inline long sys_exit(int code)          { return syscall1(SYS_EXIT, code); }
static inline long sys_exit_group(int code)    { return syscall1(SYS_EXIT_GROUP, code); }
static inline long sys_fork(void)              { return syscall0(SYS_FORK); }
static inline long sys_read(int fd, void* buf, unsigned int count) {
    return syscall3(SYS_READ, fd, (long)buf, count);
}
static inline long sys_write(int fd, const void* buf, unsigned int count) {
    return syscall3(SYS_WRITE, fd, (long)buf, count);
}
static inline long sys_open(const char* path, int flags, int mode) {
    return syscall3(SYS_OPEN, (long)path, flags, mode);
}
static inline long sys_close(int fd)           { return syscall1(SYS_CLOSE, fd); }
static inline long sys_execve(const char* path, char* const argv[], char* const envp[]) {
    return syscall3(SYS_EXECVE, (long)path, (long)argv, (long)envp);
}
static inline long sys_mount(const char* src, const char* tgt,
                             const char* fstype, unsigned long flags, const void* data) {
    return syscall5(SYS_MOUNT, (long)src, (long)tgt, (long)fstype, flags, (long)data);
}
static inline long sys_dup2(int oldfd, int newfd) { return syscall2(SYS_DUP2, oldfd, newfd); }
static inline long sys_wait4(int pid, int* status, int options, void* rusage) {
    return syscall4(SYS_WAIT4, pid, (long)status, options, (long)rusage);
}

#endif /* BOOTSTRAP_SYSCALL_H */
