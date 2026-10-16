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
#include <tss.h>
#include <elf.h>
#include <vmm.h>

task_t* current_task = NULL;
task_t* task_list_head = NULL;
static uint32_t next_pid = 1;

#include <config.h>

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

    init_task->parent = NULL;
    init_task->dead_child_pid = 0;
    init_task->dead_child_status = 0;
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

    /* vmm_user_cr3 via vmm.h */
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

    new_task->parent = current_task;
    new_task->dead_child_pid = 0;
    new_task->dead_child_status = 0;
    new_task->next = task_list_head->next;
    task_list_head->next = new_task;
}

void task_schedule(void) {
    if (!current_task) return;

    /* Переставляем указатель на следующий таск */
    current_task = current_task->next;

    if (current_task == task_list_head) {
        current_task = current_task->next;
    }

    if (!current_task) return;

    {
        /* Это слишком сильно засоряет консоль, и особо пользы не несет.
         * Но я оставлю это на всякий случай, а пока лучше не трогать иначе спам в консоль
         */
        // char b[16];
        // itoa(current_task->pid, b, 10);
        // debug_puts("Masix: task_schedule -> pid ");
        // debug_puts(b);
        // debug_puts("\n");
    }

    /* Прыжок:
     * Если у задачи cr3 равен 0, используем глобальный каталог ядра kernel_page_directory
     */
    uint32_t target_cr3 = current_task->cr3;
    if (target_cr3 == 0) {
        extern uint32_t boot_pd[1024];
        target_cr3 = (uint32_t)boot_pd;
    }
    /* Обновляем стек ядра в TSS перед каждым переключением контекста,
     * чтобы аппаратные прерывания (клавиатура, таймер) знали куда сбрасывать регистры в Ring 0.
     */
    tss_write(5, 0x10, (uint32_t)current_task->kernel_stack + 4096);
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

    child->parent = current_task;
    child->dead_child_pid = 0;
    child->dead_child_status = 0;
    child->next = task_list_head->next;
    task_list_head->next = child;

    return child->pid;
}

void task_destroy(void) {

    if (current_task->next == current_task) {
        debug_puts("Masix: Init process tried to exit! System halted.\n");
        for (;;) { asm volatile("hlt"); }
    }

    /* Запоминаем PID/статус у родителя, чтобы wait4() мог забрать */
    if (current_task->parent && current_task->parent->dead_child_pid == 0) {
        current_task->parent->dead_child_pid = current_task->pid;
        current_task->parent->dead_child_status = 0;
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

    /* Запрещаем прерывания до конца переключения: иначе таймер,
     * сработавший посреди очистки, сохранил бы ESP в task->esp уже
     * следующей (ещё не запущенной) задачи через обработчик IRQ0. */
    __asm__ __volatile__("cli");

    /* Переключаем CR3 на каталог следующей задачи ДО освобождения
     * каталога умирающей задачи: так ядро всегда работает в валидном
     * адресном пространстве, а vmm_userspace_destroy() восстановит
     * именно этот каталог после очистки старого. */
    uint32_t next_cr3 = current_task->cr3;
    if (next_cr3 == 0) {
        extern uint32_t boot_pd[1024];
        next_cr3 = (uint32_t)boot_pd;
    }
    vmm_switch_cr3(next_cr3);

    free(dead_task->user_stack);
    free(dead_task->kernel_stack);
    vmm_userspace_destroy(dead_task->cr3);
    free(dead_task);

    debug_puts("Masix: Dead task resources fully cleaned from kernel heap.\n");

    tss_write(5, 0x10, (uint32_t)current_task->kernel_stack + 4096);

    __asm__ __volatile__ (
        /* Загружаем CR3 следующей задачи */
        "mov %0, %%cr3 \n\t"

        /* Переключаем стек на сохранённый прерывательный фрейм задачи
         * (task->esp - указатель на pusha-блок, а не смещения в task_t) */
        "mov %1, %%esp \n\t"

        "popa \n\t"
        "iret \n\t"
        :
        : "r"(next_cr3), "r"(current_task->esp)
        : "memory"
    );
}
/* Максимум строк argv, которые ядро кладёт в начальный стек:
 * интерпретатор + необязательный аргумент #! + путь скрипта */
#define EXEC_MAX_ARGV 4

/* Разбирает #! скрипт (binfmt_script). Если node - скрипт,
 * возвращает 0 и заполняет interp (и необязательный sarg -
 * всё, что после интерпретатора до конца строки, как в Linux). */
static int exec_parse_script(vfs_node_t* node, char* interp, size_t interp_sz,
                                 char* sarg, size_t sarg_sz) {
    char buf[256];

    interp[0] = '\0';
    sarg[0]   = '\0';

    if (!node->read) return -1;
    int n = node->read(node, 0, sizeof(buf) - 1, (uint8_t*)buf);
    if (n < 2) return -1;
    buf[n] = '\0';

    /* Магия #! */
    if (buf[0] != '#' || buf[1] != '!') return -1;

    /* Учитываем только первую строку: #! интерпретатор [аргумент] */
    char* line = buf + 2;
    char* nl = strchr(line, '\n');
    if (nl) *nl = '\0';

    while (*line == ' ' || *line == '\t') line++;

    char* sp = line;
    while (*sp && *sp != ' ' && *sp != '\t') sp++;

    size_t len = (size_t)(sp - line);
    if (len == 0) return -1;
    if (len >= interp_sz) len = interp_sz - 1;
    memcpy(interp, line, len);
    interp[len] = '\0';

    /* Всё остальное до конца строки - один необязательный аргумент */
    while (*sp == ' ' || *sp == '\t') sp++;
    len = strlen(sp);
    while (len > 0 && (sp[len - 1] == ' ' || sp[len - 1] == '\t' ||
           sp[len - 1] == '\r')) {
        sp[--len] = '\0';
    }
    if (len > 0) {
        if (len >= sarg_sz) len = sarg_sz - 1;
        memcpy(sarg, sp, len);
        sarg[len] = '\0';
    }

    return 0;
}

/* Строит начальный стек по (примерному) стандарту Linux i386 ABI:
 * argc, argv[0..n-1], NULL (argv), NULL (envp), 0, 0 (auxv).
 * Строки копируются в пользовательский стек задачи.
 * Возвращает ESP нового процесса (указывает строго на argc). */
static uint32_t exec_setup_stack(task_t* task, const char* const* argv,
                                     int argc) {
    uint32_t* ustack = (uint32_t*)((uint32_t)task->user_stack + 4096);
    char* str_ptr = (char*)ustack;
    uint32_t str_addr[EXEC_MAX_ARGV];

    if (argc > EXEC_MAX_ARGV) argc = EXEC_MAX_ARGV;

    /* Сначала строки - снизу от вершины стека */
    for (int i = 0; i < argc; i++) {
        size_t len = strlen(argv[i]) + 1;
        str_ptr -= len;
        memcpy(str_ptr, argv[i], len);
        str_addr[i] = (uint32_t)str_ptr;
    }

    /* Пересчитываем указатель стека под выравнивание */
    uint32_t* args_ptr = (uint32_t*)((uint32_t)str_ptr & ~0xFU);

    /* argc + argv + NULL(argv) + NULL(envp) + auxv (2 слова) */
    uint32_t* esp = args_ptr - (1 + argc + 1 + 1 + 2);

    esp[0] = (uint32_t)argc;
    for (int i = 0; i < argc; i++) esp[1 + i] = str_addr[i];
    esp[1 + argc] = 0; /* конец argv */
    esp[2 + argc] = 0; /* конец envp */
    esp[3 + argc] = 0; /* auxv: AT_NULL */
    esp[4 + argc] = 0;

    return (uint32_t)esp;
}

static int32_t task_execve_impl(const char* path, const char* const* argv,
                                const char* const* envp, struct syscall_regs* regs,
                                int depth, const char* const* stack_argv,
                                int stack_argc) {
    (void)argv;
    (void)envp;
    if (!path) return -14; // -EFAULT

    debug_puts("Masix: sys_execve: ");
    debug_puts(path);
    debug_puts("\n");

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

    /* binfmt_script: #! скрипты (стадии и службы runit и т. п.).
     * Интерпретатором может быть только ELF: второй уровень
     * рекурсии (#!/! внутри скрипта-интерпретатора) запрещён,
     * как в Linux. */
    if (depth == 0) {
        char interp[128];
        char sarg[128];

        if (exec_parse_script(node, interp, sizeof(interp),
                                  sarg, sizeof(sarg)) == 0) {
            vfs_close(fd);

            debug_puts("Masix: sys_execve: #! script, interp: ");
            debug_puts(interp);
            debug_puts("\n");

            /* argv интерпретатора: [interp] ([sarg]) [скрипт] */
            const char* script_argv[EXEC_MAX_ARGV];
            int script_argc = 0;
            script_argv[script_argc++] = interp;
            if (sarg[0] != '\0') script_argv[script_argc++] = sarg;
            script_argv[script_argc++] = path;

            return task_execve_impl(interp, NULL, envp, regs,
                                        depth + 1, script_argv, script_argc);
        }
    }

    debug_puts("Masix: sys_execve reloading process image from VFS...\n");

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

    /* Начальный стек: argv от вызывающего (для #! скриптов)
     * либо классический [path] - как раньше. */
    const char* const* final_argv;
    int final_argc;
    if (stack_argv && stack_argc > 0) {
        final_argv = stack_argv;
        final_argc = stack_argc;
    } else {
        final_argv = &path;
        final_argc = 1;
    }

    uint32_t user_esp_val = exec_setup_stack(current_task, final_argv, final_argc);

    uint32_t* kstack_iret = (uint32_t*)((uint32_t)current_task->kernel_stack + 4096);

    /* Передаем новый указатель юзер-стека в iret фрейм.
     * Пользовательский ESP должен указывать строго на argc!
     */
    kstack_iret[-2] = user_esp_val;
    kstack_iret[-5] = (uint32_t)entry_point;

    /* Обнуляем регистры общего назначения перед прыжком */
    regs->eax = 0; regs->ebx = 0; regs->ecx = 0; regs->edx = 0;
    regs->esi = 0; regs->edi = 0; regs->ebp = 0;

    debug_puts("Masix: sys_execve switch complete!\n");
    return 0;
}

int32_t task_execve(const char* path, const char* const* argv,
                    const char* const* envp, struct syscall_regs* regs) {
    return task_execve_impl(path, argv, envp, regs, 0, NULL, 0);
}

__attribute__((naked)) void task_jump_to_user(void* shell_ptr, uint32_t user_esp) {
    __asm__ __volatile__(
        "cli \n\t"
        "movl 4(%%esp), %%ebx \n\t"
        "movl 8(%%esp), %%edx \n\t"

        "mov $0x23, %%ax \n\t"
        "mov %%ax, %%ds \n\t"
        "mov %%ax, %%es \n\t"
        "mov %%ax, %%fs \n\t"
        "mov %%ax, %%gs \n\t"
        "pushl $0x23 \n\t"
        "pushl %%edx \n\t"
        "pushl $0x0202 \n\t"
        "pushl $0x1B \n\t"
        "pushl %%ebx \n\t"

        "iret \n\t"
        :
        :
        : "eax", "ebx", "edx", "memory"
    );
}

