/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef MAIN_H
#define MAIN_H

/* Единственная точка входа ядра. Ее вызывает бутстрап любой архитектуры,
 * когда протокол загрузки уже разобран. */
void main_kernel_main(void);

#endif /* MAIN_H */
