/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#pragma once
#include <stdint.h>
char keyboard_getc(void);
void keyboard_init(void);

/* Положить символ в очередь ввода консоли. Очередь одна на все источники:
 * ее наполняет и обработчик клавиатуры (IRQ1), и приемник COM1 (IRQ4). */
void keyboard_push_char(char c);
