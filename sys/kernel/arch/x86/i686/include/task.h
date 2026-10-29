/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef TASK_H
#define TASK_H

#include <stdint.h>
#include <vfs.h>

struct syscall_regs;

#define TASK_RUNNING 0
#define TASK_READY   1

#define MAX_PATH 256
/* Пользовательский стек живёт по фиксированному виртуальному адресу
 * непосредственно под границей ядра (USER_TOP). Страницы берутся из PMM и
 * отображаются в адресное пространство задачи. Раньше стек лежал в куче
 * ядра, а куча - в тождественном отображении с битом USER: переполнение
 * стека вниз молча затирало соседний task_t. */
#define USER_STACK_SIZE 65536
#define USER_STACK_TOP  0xC0000000U

typedef struct task {
    uint32_t pid;
    uint32_t brk;
    uint32_t esp;
    uint32_t cr3;
    uint32_t state;
    /* Счетчик запретов вытеснения. Пока он больше нуля, планировщик
     * обязан отложить переключение: так критические секции не рвутся
     * на середине (задел под SMP и нормальные спинлоки). */
    uint32_t preempt_count;
    uint8_t* kernel_stack;
    uint8_t* user_stack;
    struct task* next;
    struct task* parent;
    int dead_child_pid;
    int dead_child_status;
    char cwd[MAX_PATH];
    /* База TLS, заданная через set_thread_area. Слот GDT один на всю
     * систему, поэтому при каждом переключении задачи её нужно
     * восстанавливать - иначе потомок, поменявший базу (например, после
     * execve), сотрёт %gs родителя и сломает его stack canary. */
    uint32_t tls_base;
    int tls_entry;
    /* Дескрипторы файлов принадлежат задаче, а не ядру целиком: два
     * процесса должны уметь независимо открывать и закрывать одни и
     * те же номера fd (getty пишет в свой tty, а не в чужой файл). */
    file_t fds[MAX_FD];
} task_t;

void task_init(void);
void task_create(void* entry_point);
void task_preempt_schedule(void);
int32_t task_fork(struct syscall_regs* regs);
int32_t task_execve(const char* path, const char* const* argv,
                    const char* const* envp, struct syscall_regs* regs);
void task_schedule(void);
void task_destroy(void);

extern task_t* current_task;
extern task_t* task_list_head;

#endif

