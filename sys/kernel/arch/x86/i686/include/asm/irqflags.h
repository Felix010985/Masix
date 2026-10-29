/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef _ASM_I686_IRQFLAGS_H
#define _ASM_I686_IRQFLAGS_H

static inline unsigned long arch_local_irq_save(void) {
    unsigned long flags;
    __asm__ __volatile__(
        "pushfl ; popl %0 ; cli"
        : "=g" (flags)
        :
        : "memory"
    );
    return flags;
}

static inline void arch_local_irq_restore(unsigned long flags) {
    __asm__ __volatile__(
        "pushl %0 ; popfl"
        :
        : "g" (flags)
        : "memory"
    );
}

static inline void arch_local_irq_disable(void) {
    __asm__ __volatile__("cli" : : : "memory");
}

static inline void arch_local_irq_enable(void) {
    __asm__ __volatile__("sti" : : : "memory");
}

#endif
