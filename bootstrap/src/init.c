/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel - bootstrap userland
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * PID 1 initramfs-инициализатор. Ядро Masix ищет в
 * initramfs жёстко прописанный /bin/init (см.
 * sys/kernel/initramfs.c) и запускает его первым.
 *
 * Задачи здесь минимальные: подмонтировать виртуальные
 * файловые системы, привязать /dev/tty0 к стандартным
 * потокам и запускать /bin/sh, перезапуская его после
 * каждого выхода.
 */
#include "syscall.h"
#include "lib.h"

static void putstr(const char* s) {
    sys_write(1, s, strlen(s));
}

int main(void);

static void mount_essential_fs(void) {
    sys_mount("none", "/proc", "proc",   0, 0);
    sys_mount("none", "/sys",   "sysfs",  0, 0);
    sys_mount("none", "/dev",   "devtmpfs", 0, 0);
}

/* Свободностоящая точка входа: ядро передаёт управление
 * прямо на e_entry ELF-файла, crt-файлов и libc нет. */
__attribute__((noreturn)) void _start(void) {
    main();
    sys_exit_group(0);
    /* main() не должна возвращаться, но на всякий случай */
    for (;;) __asm__ __volatile__("hlt");
}

int main(void) {
    mount_essential_fs();

    int fd = sys_open("/dev/tty0", O_RDWR, 0);
    if (fd >= 0) {
        sys_dup2(fd, 0); /* stdin  */
        sys_dup2(fd, 1); /* stdout */
        sys_dup2(fd, 2); /* stderr */
        if (fd > 2) sys_close(fd);
    }

    putstr("\n[ INFO ] Masix bootstrap init started (PID 1)\n");

    for (;;) {
        long pid = sys_fork();

        if (pid == 0) {
            /* Дочерний процесс: запускаем shell */
            char* argv[] = { (char*)"/bin/sh", 0 };
            char* envp[] = { 0 };
            sys_execve("/bin/sh", argv, envp);

            putstr("[ FAIL ] init: невозможно запустить /bin/sh\n");
            sys_exit(1);
        } else if (pid > 0) {
            int status = 0;
            sys_wait4((int)pid, &status, 0, 0);
            putstr("\n[ WARN ] shell exited, restarting...\n");
        } else {
            /* fork не удался - бездорожье, только halt */
            putstr("[ FAIL ] init: fork failed, halting\n");
            __asm__ __volatile__("cli");
            for (;;) __asm__ __volatile__("hlt");
        }
    }
}
