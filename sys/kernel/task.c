/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#include <task.h>
#include <syscall.h>
#include <alloc.h>
#include <string.h>
#include <vfs.h>
#include <api.h>
#include <vmm.h>

task_t* current_task = NULL;
static task_t* task_list_head = NULL;
static uint32_t next_pid = 1;

extern void* malloc(size_t size);

void task_init(void) {
    task_t* init_task = (task_t*)malloc(sizeof(task_t));
    init_task->pid = next_pid++;
    init_task->brk = 0x01100000;
    init_task->state = TASK_RUNNING;
    init_task->esp = 0;
    init_task->kernel_stack = (uint8_t*)malloc(4096);
    init_task->user_stack = (uint8_t*)malloc(4096);

    /* Задаем стартовую директорию по умолчанию */
    strcpy(init_task->cwd, "/");

    init_task->next = init_task;
    task_list_head = init_task;
    current_task = init_task;
}

void task_create(void* entry_point) {
    task_t* new_task = (task_t*)malloc(sizeof(task_t));
    new_task->pid = next_pid++;
    new_task->brk = 0x01100000;
    new_task->state = TASK_READY;
    new_task->kernel_stack = (uint8_t*)malloc(4096);
    new_task->user_stack = (uint8_t*)malloc(4096);

    uint32_t* ustack = (uint32_t*)((uint32_t)new_task->user_stack + 4096);

    ustack[-1] = 0;  // envp[0] = NULL
    ustack[-2] = 0;  // argv[1] = NULL
    ustack[-3] = (uint32_t)entry_point; // argv[0]
    ustack[-4] = 1;  // argc = 1

    uint32_t* kstack = (uint32_t*)((uint32_t)new_task->kernel_stack + 4096);

    extern uint32_t vmm_user_cr3(void);
    new_task->cr3 = vmm_user_cr3();

    kstack[-1] = 0x23;                              // User SS
    kstack[-2] = (uint32_t)new_task->user_stack + 4096 - 16; // User ESP
    kstack[-3] = 0x0202;                            // EFLAGS
    kstack[-4] = 0x1B;                              // User CS
    kstack[-5] = (uint32_t)entry_point;             // User EIP

    kstack[-6] = 0;  // EAX
    kstack[-7] = 0;  // ECX
    kstack[-8] = 0;  // EDX
    kstack[-9] = 0;  // EBX
    kstack[-10] = 0; // ESP
    kstack[-11] = 0; // EBP
    kstack[-12] = 0; // ESI
    kstack[-13] = 0; // EDI

    new_task->esp = (uint32_t)kstack - (13 * 4);

    new_task->next = task_list_head->next;
    task_list_head->next = new_task;
}

void schedule(void) {
    if (!current_task) return;

    /* Переставляем указатель на следующий таск */
    current_task = current_task->next;

    if (current_task == task_list_head) {
        current_task = current_task->next;
    }

    if (!current_task) return;

    /* Прыжок:
     * Если у задачи cr3 равен 0, используем глобальный каталог ядра kernel_page_directory
     */
    uint32_t target_cr3 = current_task->cr3;
    if (target_cr3 == 0) {
        extern uint32_t kernel_page_directory;
        target_cr3 = (uint32_t)&kernel_page_directory;
    }
    /* Обновляем стек ядра в TSS перед каждым переключением контекста,
     * чтобы аппаратные прерывания (клавиатура, таймер) знали куда сбрасывать регистры в Ring 0.
     */
    extern void write_tss(int idx, uint16_t ss0, uint32_t esp0);
    write_tss(5, 0x10, (uint32_t)current_task->kernel_stack + 4096);
    __asm__ volatile (
        /* Загружаем финальный CR3 в процессор */
        "mov %0, %%cr3 \n\t"

        /* Подменяем стек процессора на kstack задачи init */
        "mov %1, %%esp \n\t"

        /* Выталкиваем EDI, ESI, EBP, EBX, EDX, ECX, EAX */
        "popa \n\t"

        /* Аппаратный iret в Ring 3 */
        "iret \n\t"
        :
        : "r"(target_cr3), "r"(current_task->esp)
        : "memory"
    );
}


int32_t task_fork(struct syscall_regs* regs) {
    task_t* child = (task_t*)malloc(sizeof(task_t));
    if (!child) return -12; // -ENOMEM

    child->pid = next_pid++;
    child->state = TASK_READY;

    child->kernel_stack = (uint8_t*)malloc(4096);
    if (!child->kernel_stack) {
        free(child);
        return -12;
    }

    child->user_stack = (uint8_t*)malloc(4096);
    if (!child->user_stack) {
        free(child->kernel_stack);
        free(child);
        return -12;
    }

    memcpy(child->user_stack, current_task->user_stack, 4096);
    memcpy(child->kernel_stack, current_task->kernel_stack, 4096);

    strcpy(child->cwd, current_task->cwd);
    child->brk = current_task->brk;

    if (vmm_userspace_clone(current_task->cr3, &child->cr3) != 0) {
        free(child->user_stack);
        free(child->kernel_stack);
        free(child);
        return -12;
    }

    uint32_t frame_offset = (uint32_t)regs - (uint32_t)current_task->kernel_stack;
    child->esp = (uint32_t)child->kernel_stack + frame_offset;

    struct syscall_regs* child_regs = (struct syscall_regs*)child->esp;
    uint32_t* child_iret_frame = (uint32_t*)((uint32_t)child->kernel_stack + 4096);

    uint32_t parent_stack_offset = child_iret_frame[-2] - (uint32_t)current_task->user_stack;
    if (parent_stack_offset < 4096) {
        child_iret_frame[-2] = (uint32_t)child->user_stack + parent_stack_offset;
    }

    child_regs->eax = 0;

    child->next = task_list_head->next;
    task_list_head->next = child;

    return child->pid;
}

extern void free(void* ptr);
extern void puts_com1(char* a);

void task_destroy(void) {

    if (current_task->next == current_task) {
        puts_com1("Masix: Init process tried to exit! System halted.\n");
        for (;;) { asm volatile("hlt"); }
    }

    task_t* dead_task = current_task;

    task_t* prev = task_list_head;
    while (prev->next != dead_task) {
        prev = prev->next;
    }

    prev->next = dead_task->next;

    if (task_list_head == dead_task) {
        task_list_head = dead_task->next;
    }

    current_task = dead_task->next;

    free(dead_task->user_stack);
    free(dead_task->kernel_stack);
    vmm_userspace_destroy(dead_task->cr3);
    free(dead_task);

    puts_com1("Masix: Dead task resources fully cleaned from kernel heap.\n");

    __asm__ __volatile__ (
        "movl current_task, %%eax \n\t"
        "movl 4(%%eax), %%esp \n\t"

    "movl 12(%%eax), %%ebx \n\t"
    "addl $4096, %%ebx \n\t"
    "pushl %%ebx \n\t"
    "pushl $0x10 \n\t"
    "pushl $5 \n\t"
    "call write_tss \n\t"
    "addl $12, %%esp \n\t"

    "popa \n\t"
    "iret \n\t"
    :
    :
    : "eax", "ebx", "memory"
    );
}
int32_t task_execve(const char* path, const char* const* argv,
                    const char* const* envp, struct syscall_regs* regs) {
    (void)argv;
    (void)envp;
    if (!path) return -14; // -EFAULT

    /* Открываем файл через VFS */
    int32_t fd = vfs_open(path);
    if (fd < 0) {
        return -2; // -ENOENT (Файл не найден)
    }

    /* Достаем ноду из таблицы файлов */
    file_t* file_desc = &fd_table[fd];
    vfs_node_t* node = (vfs_node_t*)file_desc->private_data;

    if (!node || node->flags != VFS_FILE) {
        vfs_close(fd);
        return -13; // -EACCES (Permission denied, если это папка)
    }
    puts_com1("Masix: sys_execve reloading process image from VFS...\n");

    extern void* elf_load_node(vfs_node_t* node);
    uint32_t old_cr3 = current_task->cr3;
    void* entry_point = elf_load_node(node);
    vfs_close(fd);

    if (entry_point == NULL) {
        uint32_t failed_cr3 = vmm_user_cr3();
        if (old_cr3) vmm_switch_cr3(old_cr3);
        vmm_userspace_destroy(failed_cr3);
        return -8; // -ENOEXEC
    }

    current_task->cr3 = vmm_user_cr3();
    current_task->brk = 0x01100000;
    if (old_cr3 && old_cr3 != current_task->cr3) {
        vmm_userspace_destroy(old_cr3);
    }

    /* Build the Linux i386 initial stack: argc, argv, envp, then auxv. */
    uint32_t* ustack = (uint32_t*)((uint32_t)current_task->user_stack + 4096);

    /* Выделяем место под саму строку аргумента на верхушке стека */
    size_t path_len = strlen(path) + 1;
    char* ustack_str = (char*)ustack - path_len;
    strcpy(ustack_str, path);

    /* Пересчитываем указатель стека под выравнивание */
    uint32_t* args_ptr = (uint32_t*)((uint32_t)ustack_str & ~0xFU);

    /* Строим плоский массив по (примерному) стандарту Linux i386 ABI */
    args_ptr[-1] = 0;
    args_ptr[-2] = 0;
    args_ptr[-3] = 0;
    args_ptr[-4] = 0;
    args_ptr[-5] = (uint32_t)ustack_str;
    args_ptr[-6] = 1;

    uint32_t* kstack_iret = (uint32_t*)((uint32_t)current_task->kernel_stack + 4096);

    /* Передаем новый указатель юзер-стека в iret фрейм.
     * Пользовательский ESP должен указывать строго на argc (на элемент -6)!
     */
    uint32_t user_esp_val = (uint32_t)args_ptr - 24;
    kstack_iret[-2] = user_esp_val;
    kstack_iret[-5] = (uint32_t)entry_point;

    /* Обнуляем регистры общего назначения перед прыжком */
    regs->eax = 0; regs->ebx = 0; regs->ecx = 0; regs->edx = 0;
    regs->esi = 0; regs->edi = 0; regs->ebp = 0;

    puts_com1("Masix: sys_execve switch complete!\n");
    return 0;
}
