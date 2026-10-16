/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef ARCH_X86_BOOT_H
#define ARCH_X86_BOOT_H

#include <boot.h>

/* Объявления уровня архитектуры x86. Заголовок называется arch_boot.h,
 * чтобы не спорить с общим <boot.h> в путях включения.
 *
 * Код, общий для 32-бит и 64-бит сборки, лежит непосредственно в arch/x86,
 * а то, что зависит от разрядности (переход в реальный режим, таблицы EFI
 * с 32-битными указателями), лежит в arch/x86/i686/. */

/* Сбор информации от загрузчика в struct boot_info.
 * magic и addr это EAX и EBX на входе в ядро. */
void multiboot2_gather(uint32_t magic, uint32_t addr);

/* addr это физический адрес struct boot_params (нулевая страница LINUX),
 * который приходит в ESI при 32-битном входе протокола LINUX. */
void linux_gather(uint32_t addr);

/* Сбор информации по протоколу Limine из размеченных в образе запросов. */
void limine_gather(void);

/* Запрос линейного фреймбуфера у прошивки.
 * Пробует GOP (EFI), потом VBE (BIOS). Возвращает 0, если режим получен. */
int framebuffer_request_dispatch(struct framebuffer_info* out);

/* Пути запроса режима. Каждый определен для своей разрядности в своем файле. */
int gop_request(struct framebuffer_info* out, uint64_t system_table, int allow_mode_set);
int vbe_request(struct framebuffer_info* out);

#endif /* ARCH_X86_BOOT_H */
