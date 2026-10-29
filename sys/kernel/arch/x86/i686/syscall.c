/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#include <stdint.h>
#include <stddef.h>
#include <syscall.h>
#include <vfs.h>
#include <task.h>
#include <string.h>
#include <stdio.h>
#include <idt.h>
#include <config.h>
#include <uapi.h>
#include <linux/printk.h>

/* fcntl нужен libc и утилитам ровно настолько, чтобы узнать или
 * выставить флаги дескриптора. Блокировка файлов (F_SETLK и родня)
 * в Masix не поддерживается - на такие команды отвечаем успехом,
 * чтобы не ломать редко используемые ветки userland. */
static int32_t sys_fcntl_impl(int fd, int cmd, long arg) {
    int32_t ret;
    if (fd < 0 || fd >= MAX_FD || vfs_fd_table()[fd].type == FT_EMPTY) ret = -9; /* -EBADF */
    else switch (cmd) {
        case 0:   /* F_DUPFD */
        case 1030: /* F_DUPFD_CLOEXEC: флаг CLOEXEC мы не храним, но
                    * дескриптор обязан продублироваться, иначе оболочка
                    * получает fd 0 и работает не с тем терминалом. */
            ret = -24; /* -EMFILE */
            for (int i = (int)arg; i < MAX_FD; i++) {
                if (vfs_fd_table()[i].type == FT_EMPTY) {
                    vfs_fd_table()[i] = vfs_fd_table()[fd];
                    ret = i;
                    break;
                }
            }
            break;
        case 1: /* F_GETFD */
        case 2: /* F_SETFD */
            ret = 0;
            break;
        case 3: /* F_GETFL: считаем все дескрипторы открытыми на чтение-запись */
            ret = 2; /* O_RDWR */
            break;
        case 4: /* F_SETFL */
        default:
            ret = 0;
            break;
    }
    return ret;
}

/* Поля struct new_utsname фиксированной длины: копируем с обрезкой,
 * чтобы не выйти за 65 байт поля. */
struct new_utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

static void uts_copy(char *field, const char *value) {
    int i = 0;
    while (value[i] && i < 64) {
        field[i] = value[i];
        i++;
    }
    field[i] = '\0';
}

uint32_t syscall_handler_c(struct syscall_regs* regs) {
    {
        char b[16];
        itoa(regs->eax, b, 10);
        debug_puts("Masix: SYS enter: ");
        debug_puts(b);
        debug_puts("\n");
    }
    switch (regs->eax) {

        case MASIX_EXIT:
        {
            extern void task_destroy(void);
            task_destroy();
            return 0;
        }

        case MASIX_READ:
            return sys_read((int)regs->ebx, (char*)regs->ecx, regs->edx);

        case MASIX_WRITE:
            return sys_write(regs->ebx, (const char*)regs->ecx, regs->edx);

        case MASIX_PIPE:
        case MASIX_PIPE2:
            return vfs_pipe((int32_t*)regs->ebx);

        case MASIX_SOCKETCALL:
        {
            uint32_t *args = (uint32_t*)regs->ecx;
            if (!args) return -14;
            switch (regs->ebx) {
                case 1: return vfs_socket(args[0], args[1], args[2]);
                case 2: return vfs_socket_bind(args[0], (const void*)args[1], args[2]);
                case 3: return vfs_socket_connect(args[0], (const void*)args[1], args[2]);
                case 4: return vfs_socket_listen(args[0], args[1]);
                case 5: return vfs_socket_accept(args[0]);
                case 9: return vfs_socket_send(args[0], (const void*)args[1], args[2]);
                case 10: return vfs_socket_recv(args[0], (void*)args[1], args[2]);
                case 11: return vfs_socket_sendto(args[0], (const void*)args[1], args[2], (const void*)args[4], args[5]);
                case 12: return vfs_socket_recvfrom(args[0], (void*)args[1], args[2], (void*)args[4], (uint32_t*)args[5]);
                default: return -38;
            }
        }

        /* Новые архитектуры дают сетевые вызовы напрямую, без socketcall.
         * musl на i386 пробует их первыми, поэтому дублируем маршруты. */
        case MASIX_SOCKET:
            return vfs_socket((int)regs->ebx, (int)regs->ecx, (int)regs->edx);

        case MASIX_BIND:
            return vfs_socket_bind((int)regs->ebx, (const void*)regs->ecx, regs->edx);

        case MASIX_CONNECT:
            return vfs_socket_connect((int)regs->ebx, (const void*)regs->ecx, regs->edx);

        case MASIX_LISTEN:
            return vfs_socket_listen((int)regs->ebx, (int)regs->ecx);

        case MASIX_ACCEPT4:
            return vfs_socket_accept((int)regs->ebx);

        case MASIX_SENDTO:
            return vfs_socket_sendto((int)regs->ebx, (const void*)regs->ecx, regs->edx,
                                     (const void*)regs->esi, regs->edi);

        case MASIX_RECVFROM:
            return vfs_socket_recvfrom((int)regs->ebx, (void*)regs->ecx, regs->edx,
                                       (void*)regs->esi, (uint32_t*)regs->edi);

        case MASIX_SHUTDOWN:
            return 0;

        case MASIX_GETSOCKOPT:
        case MASIX_SETSOCKOPT:
        case MASIX_GETSOCKNAME:
        case MASIX_GETPEERNAME:
        case MASIX_SENDMSG:
        case MASIX_RECVMSG:
        case MASIX_SOCKETPAIR:
            return -LINUX_ENOSYS;

        case MASIX_BRK:
            return sys_brk(regs->ebx);

        case MASIX_FORK:
        {
            extern int32_t task_fork(struct syscall_regs* regs);
            debug_puts("Masix: sys_fork\n");
            return task_fork(regs);
        }

        /* vfork в Masix не разделяет адресное пространство с родителем:
         * отдаем полноценный fork. Дочерний процесс busybox сразу делает
         * execve, так что семантически это эквивалентно. */
        case MASIX_VFORK:
        {
            extern int32_t task_fork(struct syscall_regs* regs);
            return task_fork(regs);
        }

        case MASIX_MODIFY_LDT:
            return 0;

        /* mprotect: страницы у нас уже отображены; смена прав на лету
         * не поддерживается, но и не нужна - RELRO из musl просто
         * получит 0 и продолжит работу. */
        case MASIX_MPROTECT:
            return 0;

        case MASIX_SET_THREAD_AREA:
        {
            uint32_t* user_desc = (uint32_t*)regs->ebx;

            if (user_desc == NULL) return -9; // -EFAULT

            if ((int32_t)user_desc[0] == -1) {
                user_desc[0] = 6;
            }

            uint32_t entry_number = user_desc[0];
            uint32_t base_addr    = user_desc[1];

            { extern void itoa(int value, char* str, int base); char b[16];
              debug_puts("set_thread_area: entry="); itoa((int)entry_number,b,16); debug_puts(b);
              debug_puts(" base=0x"); itoa((int)base_addr,b,16); debug_puts(b); debug_puts("\n"); }

            extern void gdt_set_entry(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);
            gdt_set_entry(entry_number, base_addr, 0xFFFFF, 0xF2, 0xCF);

            /* Запоминаем базу в задаче: переключение контекста восстановит
             * её в GDT, даже если с тех пор слот занял другой процесс. */
            if (current_task) {
                current_task->tls_base = base_addr;
                current_task->tls_entry = (int)entry_number;
            }

            return 0;
        }

        case MASIX_WRITEV:
        {
            // ebx = fd (1 для stdout, 2 для stderr)
            // ecx = указатель на массив структур struct iovec
            // edx = количество элементов в этом массиве (iovcnt)
            int fd = regs->ebx;
            const iovec_t* iov = (const iovec_t*)regs->ecx;
            int iovcnt = regs->edx;

            if (iov == NULL) return -9; // -EFAULT

            int32_t total_written = 0;

            for (int i = 0; i < iovcnt; i++) {
                if (iov[i].iov_base != NULL && iov[i].iov_len > 0) {

                    int32_t ret = sys_write(fd, (const char*)iov[i].iov_base, iov[i].iov_len);

                    if (ret < 0) return ret;
                    total_written += ret;
                }
            }

            return total_written;
        }

        case MASIX_SET_TID_ADDRESS:
            return 1;

        case MASIX_OPEN:
        {
            const char* path = (const char*)regs->ebx;
            if (path == NULL) return -14; // -EFAULT

            int32_t fd = vfs_open_flags(path, regs->ecx);
            return fd;
        }

        case MASIX_IOCTL:
            return sys_ioctl(regs->ebx, regs->ecx, (void*)regs->edx);

        case MASIX_GETPID:
            return current_task->pid;

        case MASIX_MOUNT:
        {
            // ebx = const char* source
            // ecx = const char* target
            // edx = const char* filesystemtype
            // esi = unsigned long flags
            // edi = const void* data
            const char* source = (const char*)regs->ebx;
            const char* target = (const char*)regs->ecx;
            const char* fstype = (const char*)regs->edx;
            unsigned long flags = (unsigned long)regs->esi;
            const void* data    = (const void*)regs->edi;

            return sys_mount(source, target, fstype, flags, data);
        }

        case MASIX_RT_SIGPROCMASK:
            return 0;

        /* Погасить машину ядру нечем, поэтому честно отвечаем -EPERM,
         * как это делает Linux для непривилегированного вызова. */
        case MASIX_REBOOT:
            return -1; /* -EPERM */

        /* Права и владелец файлов в Masix не моделируются. */
        case MASIX_FCHMOD:
        case MASIX_FCHOWN32:
            return 0;

        /* Интервальные таймеры не реализованы: userland получает ноль
         * и живет без периодических сигналов. */
        case MASIX_SETITIMER:
        {
            /* ebx = which, ecx = new_value, edx = old_value */
            if (regs->edx) {
                uint32_t *old = (uint32_t*)regs->edx;
                old[0] = old[1] = old[2] = old[3] = 0;
            }
            return 0;
        }

        case MASIX_UNAME:
        {
            struct new_utsname *name = (struct new_utsname*)regs->ebx;
            if (!name) return -14; /* -EFAULT */
            uts_copy(name->sysname, "Linux");
            uts_copy(name->nodename, "masix");
            uts_copy(name->release, "5.15.0-masix");
            uts_copy(name->version, "#1 SMP Masix");
            uts_copy(name->machine, "i686");
            uts_copy(name->domainname, "(none)");
            return 0;
        }

        /* Сигналов в ядре пока нет: считаем, что ожидание всегда
         * истекает по таймауту, как это и бывает при пустой очереди. */
        case MASIX_RT_SIGTIMEDWAIT:
            return -11; /* -EAGAIN */

        case MASIX_EXIT_GROUP:
        {
            debug_puts("Masix: SYS: Process called exit_group. Cleaning up...\n");

            task_destroy();

            return 0;
        }

        case MASIX_EXECVE:
        {
            return task_execve((const char*)regs->ebx,
                               (const char* const*)regs->ecx,
                               (const char* const*)regs->edx, regs);
        }

        case MASIX_CHDIR:
        {
            const char* path = (const char*)regs->ebx;
            return sys_chdir(path);
        }

        case MASIX_FCNTL:
            return sys_fcntl_impl((int)regs->ebx, (int)regs->ecx, (long)regs->edx);

        case MASIX_DUP2:
        {
            int32_t oldfd = (int32_t)regs->ebx;
            int32_t newfd = (int32_t)regs->ecx;

            return sys_dup2(oldfd, newfd);
        }

        case MASIX_FSTAT:
            return sys_fstat(regs->ebx, (struct kstat*)regs->ecx);

        case MASIX_WAIT4:
            return sys_wait4((int)regs->ebx, (int*)regs->ecx, (int)regs->edx, (void*)regs->esi);

        case MASIX_RT_SIGACTION:
            /* Обработчики сигналов не храним: доставки сигналов нет,
             * но запрос установки/чтения нужно принять без ошибки,
             * иначе libc/userland считает среду сломанной. */
            return 0;

        case MASIX_TKILL:
        case MASIX_TGKILL:
            return 0; /* Сигналов в ядре пока нет - считаем доставленным */

        /* kill() с той же оговоркой: сигналы не доставляются, но вызов
         * не должен валить оболочку, которая опрашивает готовность
         * переднего плана через kill(-pgrp, SIGTTIN). */
        case MASIX_KILL:
            return 0;

        case MASIX_MMAP2:
            return sys_mmap2(regs->ebx, regs->ecx, regs->edx, regs->esi, (int)regs->edi, regs->ebp);

        case MASIX_CLOSE:
            return vfs_close(regs->ebx);

        case MASIX_GETCWD:
        {
            char* user_buf = (char*)regs->ebx;
            uint32_t size = regs->ecx;
            uint32_t cwd_length;

            if (!user_buf || !current_task || size == 0) return -14;
            cwd_length = strlen(current_task->cwd);
            if (cwd_length + 1 > size) return -34;
            memcpy(user_buf, current_task->cwd, cwd_length + 1);
            return (uint32_t)user_buf;
        }

        case MASIX_FCNTL64:
            return sys_fcntl_impl((int)regs->ebx, (int)regs->ecx, (long)regs->edx);

        case MASIX_PRLIMIT64:
        case MASIX_STATX:
        case MASIX_PSELECT6:
            return -LINUX_ENOSYS;

        case MASIX_UGETRLIMIT:
        case MASIX_GETRLIMIT:
            return sys_getrlimit((int)regs->ebx, (void*)regs->ecx);


        case MASIX_GETDENTS:
        case MASIX_GETDENTS64:
            return sys_getdents64(regs->ebx, (void*)regs->ecx, regs->edx);

        case MASIX_STAT64:
            return sys_fstatat(-100 /* AT_FDCWD */, (const char*)regs->ebx,
                               (struct kstat*)regs->ecx, 0);

        /* lstat64: как stat64, но не разыменовывая саму ссылку. Полноценно
         * симлинки не различаем, однако ls без этого вызова падает с
         * -ENOSYS и не может перечислить каталог. */
        case MASIX_LSTAT64:
            return sys_fstatat(-100 /* AT_FDCWD */, (const char*)regs->ebx,
                               (struct kstat*)regs->ecx, AT_SYMLINK_NOFOLLOW);

        case MASIX_READLINK:
            return sys_readlink((const char*)regs->ebx, (char*)regs->ecx, regs->edx);

        /* readlinkat: dirfd пока игнорируем, путь разрешается от cwd
         * либо как абсолютный. */
        case MASIX_READLINKAT:
            return sys_readlink((const char*)regs->ecx, (char*)regs->edx, regs->esi);

        case MASIX_ACCESS:
            return sys_access((const char*)regs->ebx, (int)regs->ecx);

        /* faccessat: dirfd игнорируем, как и в readlinkat. */
        case MASIX_FACCESSAT:
            return sys_access((const char*)regs->ecx, (int)regs->edx);

        case MASIX_FSTAT64:
            return sys_fstat64(regs->ebx, (struct kstat*)regs->ecx);

        case MASIX_FSTATAT64:
            return sys_fstatat((int)regs->ebx, (const char*)regs->ecx,
                               (struct kstat*)regs->edx, (int)regs->esi);

        case MASIX_NANOSLEEP:
        case MASIX_CLOCK_NANOSLEEP_TIME32:
            return sys_nanosleep((const void*)regs->ebx, (void*)regs->ecx);


        case MASIX_CLOCK_GETTIME32:
            return sys_clock_gettime32((int)regs->ebx, (void*)regs->ecx);


        case MASIX_CLOCK_GETTIME64:
            return sys_clock_gettime64((int)regs->ebx, (void*)regs->ecx);


        case MASIX_GETUID:
        case MASIX_GETEUID:
        case MASIX_GETGID:
        case MASIX_GETEGID:
        case MASIX_GETUID32:
        case MASIX_GETGID32:
        case MASIX_GETEUID32:
        case MASIX_GETEGID32:

            return 0;

        case MASIX_GETPPID:
            return current_task->parent ? current_task->parent->pid : 0;

        case MASIX_POLL:
        {
            struct pollfd {
                int fd;
                short events;
                short revents;
            } *fds = (struct pollfd*)regs->ebx;

            uint32_t nfds = (uint32_t)regs->ecx;

            if (fds) {
                for (uint32_t i = 0; i < nfds; i++) {
                    /* Копируем запрошенные события в возвращаемые,
                     * имитируя, что дескриптор полностью готов
                     */
                    fds[i].revents = fds[i].events & (0x0001 | 0x0004); /* POLLIN | POLLOUT */
                }
            }

            return nfds; /* Возвращаем количество готовых дескрипторов */
        }

        case MASIX_GETPGID:
            // ebx = pid. Если ebx == 0, возвращаем PGID текущего процесса.
            // Группы процессов в Masix не различаются, поэтому у каждого
            // процесса его собственная группа, совпадающая с PID.
            return (regs->ebx == 0 || (int)regs->ebx == (int)current_task->pid)
                       ? (int)current_task->pid : (int)regs->ebx;

        /* Сессии и группы процессов в Masix пока не моделируются: для
         * пользователя достаточно, что вызовы не мешают работе. init
         * (PID 1) - лидер сессии, потомки получают собственные группы. */
        case MASIX_SETSID:
            return (int)current_task->pid;

        case MASIX_SETPGID:
            return 0;

        case MASIX_GETSID:
            /* getpgid/getsid(0) для init и его потомков */
            return (regs->ebx == 0 || (int)regs->ebx == (int)current_task->pid)
                       ? (int)current_task->pid : (int)regs->ebx;

        case MASIX_GETTID:
            return (int)current_task->pid;

        case MASIX_MUNMAP:
        {
            // ebx = addr, ecx = length
            return 0;
        }

        case MASIX__LLSEEK:
        {
            // ebx = fd
            // ecx = offset_high
            // edx = offset_low
            // esi = uint64_t* result_ptr
            // edi = whence (SEEK_SET, SEEK_CUR, SEEK_END)
            int fd = regs->ebx;
            uint32_t offset_high = regs->ecx;
            uint32_t offset_low = regs->edx;
            uint64_t* res_ptr = (uint64_t*)regs->esi;
            uint32_t whence = regs->edi;

            if (fd < 0 || fd >= MAX_FD || vfs_fd_table()[fd].type == FT_EMPTY) {
                return -9; // -EBADF
            }

            vfs_node_t* node = (vfs_node_t*)vfs_fd_table()[fd].private_data;
            uint64_t offset = ((uint64_t)offset_high << 32) | offset_low;

            if (!node || vfs_fd_table()[fd].type != FT_VFS_FILE) return -29; // -ESPIPE

            if (whence == 0) {         // SEEK_SET
                vfs_fd_table()[fd].offset = (uint32_t)offset;
            } else if (whence == 1) {  // SEEK_CUR
                vfs_fd_table()[fd].offset += (uint32_t)offset;
            } else if (whence == 2) {  // SEEK_END
                vfs_fd_table()[fd].offset = node->size + (uint32_t)offset;
            } else {
                return -22; // -EINVAL
            }

            if (res_ptr != NULL) {
                *res_ptr = (uint64_t)vfs_fd_table()[fd].offset;
            }

            return 0;
        }

        default:
        {
            char stub_buf[16];
            itoa(regs->eax, stub_buf, 10);

            printk("Masix: SYS: Unimplemented Linux syscall requested: %s\n", stub_buf);

            return -LINUX_ENOSYS;
        }
    }
}

__attribute__((naked)) void syscall_handler_asm(void) {
    __asm__ __volatile__ (
        "pusha \n\t"

        "mov $0x10, %ax \n\t"
        "mov %ax, %ds \n\t"
        "mov %ax, %es \n\t"

        "sti \n\t"

        "push %esp \n\t"
        "call syscall_handler_c \n\t"
        "add $4, %esp \n\t"

        "mov %eax, 28(%esp) \n\t"

        "cli \n\t"
        "popa \n\t"

        "push %ax \n\t"
        "mov $0x23, %ax \n\t"
        "mov %ax, %ds \n\t"
        "mov %ax, %es \n\t"
        "mov $0x33, %ax \n\t"
        "mov %ax, %fs \n\t"
        "mov %ax, %gs \n\t"
        "pop %ax \n\t"

        "iret \n\t"
    );
}

void syscall_init(void) {
    idt_register_handler(0x80, (uint32_t)syscall_handler_asm, 0xEE);
}
