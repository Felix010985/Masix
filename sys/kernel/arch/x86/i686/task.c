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
#include <pmm.h>
#include <string.h>
#include <vfs.h>
#include <tss.h>
#include <elf.h>
#include <vmm.h>

task_t* current_task = NULL;
task_t* task_list_head = NULL;
static uint32_t next_pid = 1;

#include <config.h>

/* Вытеснение по тику таймера. Пока это только каркас: счетчик
 * preempt_count уже защищает критические секции, а сам переключатель
 * использует ту же схему, что task_schedule(), но без iret-фрейма -
 * возврат идет в прерванный код ядра. */
void task_preempt_schedule(void) {
    if (!current_task || current_task->preempt_count > 0) return;

    task_t* prev = current_task;
    task_t* next = current_task->next;
    if (next == task_list_head) next = next->next;

    current_task = next;

    /* Обновляем стек в TSS для переходов из Ring 3 */
    tss_write(5, 0x10, (uint32_t)next->kernel_stack + 4096);

    /* Ассемблерная подмена стеков ядра */
    __asm__ volatile (
        "pushl %%ebp \n\t"
        "pushl %%ebx \n\t"
        "pushl %%esi \n\t"
        "pushl %%edi \n\t"

        "movl %%esp, %0 \n\t" /* Сохраняем ESP старой задачи */
        "movl %1, %%esp \n\t" /* Загружаем ESP новой задачи */

        "movl %%cr3, %%eax \n\t"
        "cmpl %2, %%eax \n\t"
        "je 1f \n\t"
        "movl %2, %%cr3 \n\t" /* Меняем каталог страниц, если нужно */
        "1: \n\t"

        "popl %%edi \n\t"
        "popl %%esi \n\t"
        "popl %%ebx \n\t"
        "popl %%ebp \n\t"
        : "=m"(prev->esp)
        : "m"(next->esp), "r"(next->cr3)
        : "eax", "memory"
    );
}

void task_init(void) {
    task_t* init_task = (task_t*)malloc(sizeof(task_t));
    /* Заглушка ядра не должна занимать PID 1: его получает
     * userland-init, которому этот номер нужен по контракту (busybox
     * отказывается работать, если getpid() != 1). */
    init_task->pid = 0;
    init_task->brk = 0x01100000;
    init_task->state = TASK_RUNNING;
    init_task->preempt_count = 0;
    init_task->esp = 0;
    init_task->kernel_stack = (uint8_t*)malloc(4096);
    /* Заглушка ядра не выходит в Ring 3, пользовательский стек ей не нужен. */
    init_task->user_stack = NULL;

    /* Задаем стартовую директорию по умолчанию */
    strcpy(init_task->cwd, "/");

    init_task->parent = NULL;
    init_task->dead_child_pid = 0;
    init_task->dead_child_status = 0;
    init_task->tls_base = 0;
    init_task->tls_entry = -1;

    /* Перенимаем stdio (fd 0/1/2), которые vfs_init() уже повесил на
     * console ещё до появления задач: current_task здесь ещё NULL, так
     * что vfs_fd_table() отдаёт загрузочную таблицу. */
    memcpy(init_task->fds, vfs_fd_table(), sizeof(init_task->fds));

    init_task->next = init_task;
    task_list_head = init_task;
    current_task = init_task;
}

/* Объявления определений, которые лежат ниже: task_create собирает
 * начальный стек PID 1 и обращается к этому окружению и хелперу. */
static uint32_t exec_setup_stack(task_t* task, const char* const* argv, int argc,
                                 const char* const* envp, int envc);
static const char* const exec_default_envp[];
static const int exec_default_envc;

/* Отводит задаче пользовательский стек: USER_STACK_SIZE байт прямо под
 * границей ядра. Страницы берём из PMM и мапим в текущий каталог (тот, что
 * сейчас активен у вызывающего), поэтому вызывать её нужно уже после
 * подмены каталога задачи (task_create/execve). Ниже стека отображения нет:
 * переполнение вниз приведёт к #PF, а не к записи в чужую память ядра. */
static void user_stack_map(task_t* task) {
    uint32_t base = USER_STACK_TOP - USER_STACK_SIZE;

    for (uint32_t off = 0; off < USER_STACK_SIZE; off += PAGE_SIZE) {
        uint32_t phys = pmm_alloc_zeroed();
        if (!phys) break;
        if (vmm_user_map(base + off, phys, PAGE_WRITE) != 0) break;
    }

    task->user_stack = (uint8_t*)base;
}

void task_create(void* entry_point) {
    task_t* new_task = (task_t*)malloc(sizeof(task_t));
    new_task->pid = next_pid++;
    new_task->brk = 0x01100000;
    new_task->state = TASK_READY;
    new_task->preempt_count = 0;
    new_task->kernel_stack = (uint8_t*)malloc(4096);

    /* Каталог страниц PID 1 уже создан elf_load_binary(), поэтому стек
     * мапим сразу - exec_setup_stack() пишет в него по виртуальному адресу. */
    user_stack_map(new_task);

    strcpy(new_task->cwd, "/");

    /* PID 1 запускается ядром напрямую, без execve, поэтому argv/envp
     * задаем здесь. argv[0] важен busybox: по имени applet'а он решает,
     * что запускать - в данном случае init. */
    const char* argv[1];
    argv[0] = "/bin/init";

    uint32_t user_esp = exec_setup_stack(new_task, argv, 1,
                                         exec_default_envp, exec_default_envc);

    uint32_t* kstack = (uint32_t*)((uint32_t)new_task->kernel_stack + 4096);

    /* vmm_user_cr3 via vmm.h */
    new_task->cr3 = vmm_user_cr3();

    kstack[-1] = 0x23;                              // User SS
    kstack[-2] = user_esp;                          // User ESP
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
    new_task->tls_base = 0;
    new_task->tls_entry = -1;

    /* PID 1 наследует stdio от ядра-заглушки, иначе getty/init не смогут
     * ни читать с консоли, ни писать в неё. */
    memcpy(new_task->fds, current_task->fds, sizeof(new_task->fds));

    new_task->next = task_list_head->next;
    task_list_head->next = new_task;
}

void task_schedule(void) {    if (!current_task) return;

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
    /* Держим g_user в согласии с задачей, которая сейчас побежит. */
    vmm_bind_user_cr3(current_task->cr3);
    /* Обновляем стек ядра в TSS перед каждым переключением контекста,
     * чтобы аппаратные прерывания (клавиатура, таймер) знали куда сбрасывать регистры в Ring 0.
     */
    tss_write(5, 0x10, (uint32_t)current_task->kernel_stack + 4096);

    /* Слот TLS в GDT общий на всю систему, а сегментный регистр %gs
     * кэширует дескриптор: одного переписывания GDT мало. Восстанавливаем
     * дескриптор задачи и заново грузим селектор, иначе вернувшийся процесс
     * получит чужую базу TLS и сломает свой stack canary. */
    {
        extern void gdt_set_entry(int num, uint32_t base, uint32_t limit,
                                  uint8_t access, uint8_t gran);
        uint16_t tls_sel = 0;
        if (current_task->tls_entry >= 0) {
            gdt_set_entry(current_task->tls_entry, current_task->tls_base,
                          0xFFFFF, 0xF2, 0xCF);
            tls_sel = (uint16_t)((current_task->tls_entry << 3) | 3);
        }
        __asm__ volatile ("movw %0, %%gs" : : "r"(tls_sel) : "memory");
    }
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
    child->preempt_count = 0;

    child->kernel_stack = (uint8_t*)malloc(4096);
    if (!child->kernel_stack) {
        free(child);
        return -12;
    }

    /* Стек потомка не выделяем: vmm_userspace_clone() ниже клонирует
     * страницы адресного пространства, а стек лежит по общему для родителя
     * и потомка фиксированному адресу. */
    child->user_stack = current_task->user_stack;

    memcpy(child->kernel_stack, current_task->kernel_stack, 4096);

    strcpy(child->cwd, current_task->cwd);
    child->brk = current_task->brk;

    if (vmm_userspace_clone(current_task->cr3, &child->cr3) != 0) {
        free(child->kernel_stack);
        free(child);
        return -12;
    }

    uint32_t frame_offset = (uint32_t)regs - (uint32_t)current_task->kernel_stack;
    child->esp = (uint32_t)child->kernel_stack + frame_offset;

    struct syscall_regs* child_regs = (struct syscall_regs*)child->esp;
    uint32_t* child_iret_frame = (uint32_t*)((uint32_t)child->kernel_stack + 4096);

    uint32_t parent_stack_offset = child_iret_frame[-2] - (uint32_t)current_task->user_stack;
    if (parent_stack_offset < USER_STACK_SIZE) {
        child_iret_frame[-2] = (uint32_t)child->user_stack + parent_stack_offset;
    }

    child_regs->eax = 0;

    child->parent = current_task;
    child->dead_child_pid = 0;
    child->dead_child_status = 0;
    child->tls_base = current_task->tls_base;
    child->tls_entry = current_task->tls_entry;

    /* fork() копирует таблицу дескрипторов: потомок видит те же fd,
     * открытые родителем (общие tty/файлы). Увеличиваем refcount узлов,
     * чтобы vfs_close() в одном процессе не освободил узел, которым
     * пользуется другой. */
    memcpy(child->fds, current_task->fds, sizeof(child->fds));
    for (int i = 0; i < MAX_FD; i++) {
        if (child->fds[i].type == FT_VFS_FILE && child->fds[i].private_data) {
            vfs_node_t* node = (vfs_node_t*)child->fds[i].private_data;
            node->refcount++;
        }
    }

    /* Вставка в список задач - атомарно: задачу может вытеснить
     * таймер, и параллельный fork испортил бы цепочку. */
    uint32_t fork_flags;
    __asm__ __volatile__("pushfl\n\tpopl %0\n\tcli" : "=r"(fork_flags) :: "memory");
    child->next = task_list_head->next;
    task_list_head->next = child;
    __asm__ __volatile__("pushl %0\n\tpopfl" :: "r"(fork_flags) : "memory");

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

    /* Дальше меняем общий список задач и адресные пространства -
     * вытеснение тут недопустимо. Раньше cli стоял ниже, но цепочку
     * тоже нужно рвать атомарно. */
    __asm__ __volatile__("cli");

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

    /* Переключаем CR3 на каталог следующей задачи ДО освобождения
     * каталога умирающей задачи: так ядро всегда работает в валидном
     * адресном пространстве, а vmm_userspace_destroy() восстановит
     * именно этот каталог после очистки старого. */
    uint32_t next_cr3 = current_task->cr3;
    if (next_cr3 == 0) {
        extern uint32_t boot_pd[1024];
        next_cr3 = (uint32_t)boot_pd;
    }
    vmm_bind_user_cr3(current_task->cr3);
    vmm_switch_cr3(next_cr3);

    /* user_stack - виртуальный адрес в адресном пространстве задачи; его
     * страницы освобождает vmm_userspace_destroy() вместе с остальными. */
    free(dead_task->kernel_stack);
    vmm_userspace_destroy(dead_task->cr3);
    free(dead_task);

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
/* Максимум строк argv/envp, которые ядро кладёт в начальный стек:
 * интерпретатор + необязательный аргумент #! + путь скрипта, а также
 * аргументы, которые userland передал в execve (getty 38400 tty1 и т. п.). */
#define EXEC_MAX_ARGV 32
#define EXEC_MAX_ENVP 32

/* Пары auxv для начального стека (Linux i386). Динамический линковщик
 * musl без них не может найти ни программу, ни собственную базу. */
#define AT_NULL   0
#define AT_PHDR   3
#define AT_PHENT  4
#define AT_PHNUM  5
#define AT_PAGESZ 6
#define AT_BASE   7
#define AT_ENTRY  9

/* Минимальное окружение для execve: динамическим программам нужен
 * хотя бы PATH, иначе busybox не найдёт свои applet'ы по имени. */
static const char* const exec_default_envp[] = {
    "PATH=/sbin:/usr/sbin:/bin:/usr/bin",
    "HOME=/",
    "TERM=linux",
};
static const int exec_default_envc = 3;

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

/* Строит начальный стек по стандарту Linux i386 ABI:
 * argc, argv[0..n-1], NULL, envp[0..m-1], NULL, auxv (пары), AT_NULL.
 * Строки копируются в пользовательский стек задачи.
 * Возвращает ESP нового процесса (указывает строго на argc). */
static uint32_t exec_setup_stack(task_t* task, const char* const* argv, int argc,
                                     const char* const* envp, int envc) {
    uint32_t* ustack = (uint32_t*)((uint32_t)task->user_stack + USER_STACK_SIZE);
    char* str_ptr = (char*)ustack;
    uint32_t str_addr[EXEC_MAX_ARGV];
    uint32_t env_addr[EXEC_MAX_ENVP];

    if (argc > EXEC_MAX_ARGV) argc = EXEC_MAX_ARGV;
    if (envc > EXEC_MAX_ENVP) envc = EXEC_MAX_ENVP;
    if (argc < 0) argc = 0;
    if (envc < 0) envc = 0;

    /* Сначала строки - снизу от вершины стека. envp кладем первыми,
     * чтобы в памяти они оказались выше argv, как в Linux. */
    for (int i = 0; i < envc; i++) {
        size_t len = strlen(envp[i]) + 1;
        str_ptr -= len;
        memcpy(str_ptr, envp[i], len);
        env_addr[i] = (uint32_t)str_ptr;
    }
    for (int i = 0; i < argc; i++) {
        size_t len = strlen(argv[i]) + 1;
        str_ptr -= len;
        memcpy(str_ptr, argv[i], len);
        str_addr[i] = (uint32_t)str_ptr;
    }

    /* Пересчитываем указатель стека под выравнивание */
    uint32_t str_base = (uint32_t)str_ptr & ~0xFU;

    /* argc + argv + NULL(argv) + envp + NULL(envp) + пары auxv + AT_NULL */
    int words = 1 + argc + 1 + envc + 1 + 6 * 2 + 2;
    uint32_t* esp = (uint32_t*)(str_base - (uint32_t)words * 4);

    uint32_t* p = esp;
    *p++ = (uint32_t)argc;
    for (int i = 0; i < argc; i++) *p++ = str_addr[i];
    *p++ = 0; /* конец argv */
    for (int i = 0; i < envc; i++) *p++ = env_addr[i];
    *p++ = 0; /* конец envp */

    *p++ = AT_PHDR;   *p++ = elf_aux.phdr;
    *p++ = AT_PHENT;  *p++ = elf_aux.phent;
    *p++ = AT_PHNUM;  *p++ = elf_aux.phnum;
    *p++ = AT_PAGESZ; *p++ = PAGE_SIZE;
    *p++ = AT_BASE;   *p++ = elf_aux.base;
    *p++ = AT_ENTRY;  *p++ = elf_aux.entry;
    *p++ = AT_NULL;   *p++ = 0;

    return (uint32_t)esp;
}

static int32_t task_execve_impl(const char* path, const char* const* argv,
                                const char* const* envp, struct syscall_regs* regs,
                                int depth, const char* const* stack_argv,
                                int stack_argc);

/* Копия строки в ядерную память. Нужна, чтобы пережить подмену
 * адресного пространства: userland-строки могут лежать в куче
 * (brk/mmap), которая исчезнет вместе со старым каталогом страниц. */
static char* exec_strdup(const char* s) {
    size_t n = strlen(s) + 1;
    char* d = (char*)malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}

static int32_t task_execve_core(const char* path, const char* const* argv,
                                const char* const* envp, struct syscall_regs* regs,
                                int depth, const char* const* stack_argv,
                                int stack_argc) {
    /* argv/envp приходят из userland как user-указатели. Пока каталог
     * страниц ещё старый, ядро может их прочитать; снимок делается до
     * elf_load_node, потому что после подмены адресного пространства
     * user-строки станут недоступны. */
    const char* const* user_argv = argv;
    const char* const* user_envp = envp;
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
    file_t* file_desc = &vfs_fd_table()[fd];
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

    /* Снимок argv/envp до подмены адресного пространства: строки из
     * user-кучи после elf_load_node становятся недоступны, а стек
     * нового процесса строится уже в другом каталоге страниц. */
    const char* snap_argv[EXEC_MAX_ARGV + 1];
    int snap_argc = 0;
    const char* snap_envp[EXEC_MAX_ENVP + 1];
    int snap_envc = 0;
    int argv_owned = 0, envp_owned = 0;
    int have_envp = (user_envp != NULL);

    if (stack_argv && stack_argc > 0) {
        int n = stack_argc > EXEC_MAX_ARGV ? EXEC_MAX_ARGV : stack_argc;
        for (int i = 0; i < n; i++) snap_argv[i] = stack_argv[i];
        snap_argc = n;
    } else if (user_argv) {
        argv_owned = 1;
        while (snap_argc < EXEC_MAX_ARGV && user_argv[snap_argc]) {
            snap_argv[snap_argc] = exec_strdup(user_argv[snap_argc]);
            snap_argc++;
        }
    }
    snap_argv[snap_argc] = NULL;

    if (user_envp) {
        envp_owned = 1;
        while (snap_envc < EXEC_MAX_ENVP && user_envp[snap_envc]) {
            snap_envp[snap_envc] = exec_strdup(user_envp[snap_envc]);
            snap_envc++;
        }
    }
    snap_envp[snap_envc] = NULL;

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

    /* Начальный стек строится из ядерного снимка argv/envp (см. выше). */
    const char* const* final_argv;
    int final_argc;
    if (snap_argc > 0) {
        final_argv = snap_argv;
        final_argc = snap_argc;
    } else {
        final_argv = &path;
        final_argc = 1;
    }

    const char* const* final_envp;
    int final_envc;
    if (have_envp) {
        final_envp = snap_envp;
        final_envc = snap_envc;
    } else {
        final_envp = exec_default_envp;
        final_envc = exec_default_envc;
    }

    /* Новое адресное пространство уже активно (elf_load_node), поэтому
     * отводим в нём свежий пользовательский стек. */
    user_stack_map(current_task);

    uint32_t user_esp_val = exec_setup_stack(current_task, final_argv, final_argc,
                                             final_envp, final_envc);

    /* Снимок больше не нужен: строки уже скопированы на стек. */
    if (argv_owned) {
        for (int i = 0; i < snap_argc; i++) free((void*)snap_argv[i]);
    }
    if (envp_owned) {
        for (int i = 0; i < snap_envc; i++) free((void*)snap_envp[i]);
    }

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

/* execve подменяет адресное пространство через глобальное состояние
 * загрузчика (g_user, elf_aux). Планировщик вытесняет задачу по тику PIT,
 * поэтому на время загрузки образа запрещаем прерывания: иначе тик может
 * переключить контекст посреди разбора ELF, и следующий execve перезапишет
 * g_user чужой копией - страницы уедут не в тот каталог. После возврата
 * флаги восстанавливаются, а userland все равно получает их из iret-фрейма. */
static int32_t task_execve_impl(const char* path, const char* const* argv,
                                const char* const* envp, struct syscall_regs* regs,
                                int depth, const char* const* stack_argv,
                                int stack_argc) {
    uint32_t flags;
    __asm__ __volatile__("pushfl\n\tpopl %0\n\tcli" : "=r"(flags) :: "memory");

    int32_t ret = task_execve_core(path, argv, envp, regs, depth,
                                   stack_argv, stack_argc);

    __asm__ __volatile__("pushl %0\n\tpopfl" :: "r"(flags) : "memory");
    return ret;
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

