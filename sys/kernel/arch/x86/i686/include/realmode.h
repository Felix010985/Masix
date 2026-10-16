/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef ARCH_I686_REALMODE_H
#define ARCH_I686_REALMODE_H

#include <stdint.h>

/* Регистры для INT 10h. Порядок полей совпадает с образом регистров,
 * который читает 16-битный станок: поле flags не используется. */
struct realmode_regs {
    uint16_t ax;
    uint16_t bx;
    uint16_t cx;
    uint16_t dx;
    uint16_t si;
    uint16_t di;
    uint16_t flags;
    uint16_t es;
};

/* Выполнить INT 10h в реальном режиме.
 * Функция обязана вызываться по своему тождественному (физическому) адресу:
 * на время вызова страницы выключаются. */
void realmode_int10(struct realmode_regs* regs);

#endif /* ARCH_I686_REALMODE_H */
