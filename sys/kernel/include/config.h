/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef KERNEL_CONFIG_H
#define KERNEL_CONFIG_H

/* Настройки, сгенерированные tools/configure (если есть).
 * Без этого файла работают значения по умолчанию ниже. */
#if __has_include(<build_config.h>)
#include <build_config.h>
#endif

/* Уровни отладки */
#define DEBUG_LEVEL_NONE   0
#define DEBUG_LEVEL_ERROR  1
#define DEBUG_LEVEL_INFO   2
#define DEBUG_LEVEL_DEBUG  3

#ifndef DEBUG_LEVEL
#define DEBUG_LEVEL DEBUG_LEVEL_INFO
#endif

/* Бэкенд отладочного вывода.
 *
 * debug_puts() - единственный разрешённый способ выводить отладочный
 * текст из ядра: напрямую вызывать serial_puts() (или другой бэкенд)
 * больше не нужно. Выберите ровно один бэкенд ниже, либо передайте его
 * через CFLAGS при сборке ядра (например, -DDEBUG_OUTPUT_PRINTK),
 * либо через tools/configure:
 *
 *   DEBUG_OUTPUT_SERIAL  -> serial_puts()  (UART 8250, по умолчанию)
 *   DEBUG_OUTPUT_PRINTK  -> printk()       (ядерный printk)
 *   DEBUG_OUTPUT_STDIO   -> printf()       (ядерная stdio)
 */
#if !defined(DEBUG_OUTPUT_SERIAL) && !defined(DEBUG_OUTPUT_PRINTK) && !defined(DEBUG_OUTPUT_STDIO)
#define DEBUG_OUTPUT_SERIAL
#endif

/* Уровень ниже INFO полностью выключает отладочный вывод.
 * Отдельные уровни для сообщений появятся позже, а пока
 * все debug_puts() - информационные. */
#if DEBUG_LEVEL >= DEBUG_LEVEL_INFO

#ifdef DEBUG_OUTPUT_SERIAL
#include <drivers/serial.h>
#define debug_puts(str) serial_puts(str)
#elif defined(DEBUG_OUTPUT_PRINTK)
#include <linux/printk.h>
#define debug_puts(str) printk("%s", (str))
#else /* DEBUG_OUTPUT_STDIO */
#include <stdio.h>
#define debug_puts(str) printf("%s", (str))
#endif

#else /* DEBUG_LEVEL < DEBUG_LEVEL_INFO: отладка выключена */
#define debug_puts(str)
#endif

#endif /* KERNEL_CONFIG_H */
