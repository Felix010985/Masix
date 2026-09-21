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
#include <api.h>

extern int32_t k_sys_write(int fd, const char* buf, uint32_t count);
extern int32_t k_sys_read(int fd, char* buf, uint32_t count);
extern void idt_register_handler(uint8_t vector, uint32_t handler_addr, uint8_t flags);
extern void itoa(int value, char* str, int base);
extern void puts_com1(const char* s);

// struct iovec {
//     void* iov_base;
//     uint32_t iov_len;
// };

uint32_t syscall_handler_c(struct syscall_regs* regs) {
    switch (regs->eax) {

        case MASIX_EXIT:
        {
            extern void task_destroy(void);
            task_destroy();
            return 0;
        }

        case MASIX_READ:
        {
            int fd = regs->ebx;
            char* user_buf = (char*)regs->ecx;
            uint32_t count = regs->edx;

            if (fd < 3) {
                extern int32_t k_sys_read(int fd, char* buf, uint32_t count);
                return k_sys_read(fd, user_buf, count);
            }

            if (fd >= 32 || fd_table[fd].type == 0) { // FT_EMPTY
                return -9; // -EBADF
            }
            return vfs_read(fd, user_buf, count);
        }

        case MASIX_WRITE:
            return k_sys_write(regs->ebx, (const char*)regs->ecx, regs->edx);

        case MASIX_PIPE:
        case MASIX_PIPE2:
            return vfs_pipe((int32_t*)regs->ebx);

        case MASIX_SOCKETCALL:
        {
            uint32_t *args = (uint32_t*)regs->ebx;
            if (!args) return -14;
            switch (regs->ecx) {
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

        case MASIX_BRK:
        {
            uint32_t new_brk = regs->ebx;

            if (new_brk == 0) {
                return current_task->brk;
            }

            if (new_brk >= current_task->brk) {
                current_task->brk = new_brk;
            }
            return current_task->brk;
        }

        case MASIX_FORK:
        {
            extern int32_t task_fork(struct syscall_regs* regs);
            return task_fork(regs);
        }

        case MASIX_MODIFY_LDT:
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

            extern void gdt_set_entry(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);
            gdt_set_entry(entry_number, base_addr, 0xFFFFF, 0xF2, 0xCF);

            return 0;
        }

        case MASIX_WRITEV:
        {
            // ebx = fd (1 для stdout, 2 для stderr)
            // ecx = указатель на массив структур struct iovec
            // edx = количество элементов в этом массиве (iovcnt)
            int fd = regs->ebx;
            const struct iovec* iov = (const struct iovec*)regs->ecx;
            int iovcnt = regs->edx;

            if (iov == NULL) return -9; // -EFAULT

            int32_t total_written = 0;

            for (int i = 0; i < iovcnt; i++) {
                if (iov[i].iov_base != NULL && iov[i].iov_len > 0) {

                    extern int32_t k_sys_write(int fd, const char* buf, uint32_t count);
                    int32_t ret = k_sys_write(fd, (const char*)iov[i].iov_base, iov[i].iov_len);

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

            int32_t fd = vfs_open(path);
            if (fd < 0) {
                return -2;
            }
            return fd;
        }

        case MASIX_IOCTL:
            return 0;

        case MASIX_GETPID:
            extern task_t* current_task;
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

            extern int32_t sys_mount(const char* source, const char* target, const char* filesystemtype, unsigned long flags, const void* data);

            return sys_mount(source, target, fstype, flags, data);
        }

        case MASIX_RT_SIGPROCMASK:
            return 0;

        case MASIX_EXIT_GROUP:
        {
            puts_com1("Masix: SYS: Process called exit_group. Cleaning up...\n");

            extern void task_destroy(void);
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
            return 0;

        case MASIX_DUP2:
        {
            int32_t oldfd = (int32_t)regs->ebx;
            int32_t newfd = (int32_t)regs->ecx;

            extern int32_t sys_dup2(int32_t oldfd, int32_t newfd);
            return sys_dup2(oldfd, newfd);
        }

        case MASIX_FSTAT:
            return 0;

        case MASIX_WAIT4:
        {
            int32_t pid = (int32_t)regs->ebx;
            int32_t* wstatus = (int32_t*)regs->ecx;

            if (wstatus) {
                *wstatus = 0;
            }

            return pid;
        }

        case MASIX_RT_SIGACTION:
            return 0;

        case MASIX_TKILL:
            return 0;

        case MASIX_MMAP2:
        {
            uint32_t length = regs->ecx;

            uint32_t aligned_len = (length + 4095) & ~4095;

            uint32_t allocated_addr = current_task->brk;
            current_task->brk += aligned_len;

            return allocated_addr;
        }

        case MASIX_CLOSE:
            return vfs_close(regs->ebx);

        case MASIX_GETCWD:
        {
            // ebx = char* buf, ecx = unsigned long size
            char* user_buf = (char*)regs->ebx;
            uint32_t size = regs->ecx;

            if (user_buf != NULL && size > 2) {
                user_buf[0] = '/';
                user_buf[1] = '\0';
                return (uint32_t)user_buf;
            }
            return 0;
        }

        case MASIX_FCNTL64:
            // ebx = fd, ecx = cmd, edx = arg
            return 0;

        case MASIX_UGETRLIMIT:
            // ebx = resource, ecx = struct rlimit*
            return 0;

        case MASIX_GETDENTS:
            // ebx = fd, ecx = struct linux_dirent*, edx = count
            return 0;

        case MASIX_GETUID:
        case MASIX_GETEUID:
        case MASIX_GETGID:
        case MASIX_GETGID32:
        case MASIX_GETEGID:

            return 0;

        case MASIX_STAT64:
        case MASIX_FSTAT64:
        case MASIX_FSTAT64_ALT:
        case MASIX_NANOSLEEP:
            // ebx = fd или путь, ecx = struct stat*
            return 0;

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

        case MASIX_CLOCK_GETTIME64:
            // ebx = clock_id, ecx = struct timespec64*
            return 0;

        case MASIX_GETPGID:
            // ebx = pid. Если ebx == 0, возвращаем PGID текущего процесса.
            return 1;

        case MASIX_PRLIMIT64:
            // ebx = pid, ecx = resource, edx = new_limit, esi = old_limit
            return 0;

        case MASIX_STATX:
            // ebx = dfd, ecx = filename, edx = flags, esi = mask, edi = buffer
            return 0;

        case MASIX_PSELECT6:
            // ebx = n, ecx = inp, edx = outp, esi = exp, edi = tsp
            return 0;

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

            struct fd_entry {
                int type;
                uint32_t offset;
                void* private_data;
            };
            //extern struct fd_entry fd_table[32];

            if (fd >= 32 || fd_table[fd].type == 0) {
                return -9; // -EBADF
            }

            vfs_node_t* node = (vfs_node_t*)fd_table[fd].private_data;
            uint64_t offset = ((uint64_t)offset_high << 32) | offset_low;

            if (whence == 0) {         // SEEK_SET
                fd_table[fd].offset = (uint32_t)offset;
            } else if (whence == 1) {  // SEEK_CUR
                fd_table[fd].offset += (uint32_t)offset;
            } else if (whence == 2) {  // SEEK_END
                fd_table[fd].offset = node->size + (uint32_t)offset;
            } else {
                return -22; // -EINVAL
            }

            if (res_ptr != NULL) {
                *res_ptr = (uint64_t)fd_table[fd].offset;
            }

            return 0;
        }

        default:
            char stub_buf[16];
            itoa(regs->eax, stub_buf, 10);

            puts_com1("Masix: SYS: Unimplemented Linux syscall requested: ");
            puts_com1(stub_buf);
            puts_com1("\n");

            return -LINUX_ENOSYS;
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
