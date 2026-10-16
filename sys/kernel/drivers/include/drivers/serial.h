/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#define COM1 0x3F8

// static inline void outb(uint16_t port, uint8_t val) {}

// static inline uint8_t inb(uint16_t port) {}

void serial_init(void);

void serial_putc(char a);

void serial_puts(const char* str);
