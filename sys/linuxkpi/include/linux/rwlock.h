/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix LinuxKPI
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef _LINUX_RWLOCK_H
#define _LINUX_RWLOCK_H

#include <asm/irqflags.h>

typedef struct {
    volatile int placeholder;
} rwlock_t;

#define RW_LOCK_UNLOCKED (rwlock_t){ .placeholder = 0 }

/* Внешнее объявление для доступа к текущей задаче ядра */
extern task_t* current_task;

#define read_lock(lock) \
do { \
    (void)(lock); \
    if (current_task) current_task->preempt_count++; \
} while(0)

#define read_unlock(lock) \
do { \
    (void)(lock); \
    if (current_task) current_task->preempt_count--; \
} while(0)

#define write_lock(lock)   read_lock(lock)
#define write_unlock(lock) read_unlock(lock)

#define read_lock_irqsave(lock, flags) \
do { \
    flags = arch_local_irq_save(); \
    read_lock(lock); \
} while(0)

#define read_unlock_irqrestore(lock, flags) \
do { \
    read_unlock(lock); \
    arch_local_irq_restore(flags); \
} while(0)

#define write_lock_irqsave(lock, flags)   read_lock_irqsave(lock, flags)
#define write_unlock_irqrestore(lock, flags) read_unlock_irqrestore(lock, flags)

#endif
