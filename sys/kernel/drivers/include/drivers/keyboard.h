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

/* Модификаторы, сопровождающие нажатие. */
#define KBD_MOD_SHIFT 0x01
#define KBD_MOD_CTRL  0x02
#define KBD_MOD_ALT   0x04

/*
 * Приемник нажатий. Драйвер клавиатуры не знает, что находится над ним,
 * поэтому каждое нажатие отдает наверх: scancode - код клавиши (make),
 * c - раскодированный ASCII (0, если символ не получается - например,
 * для функциональных клавиш), mods - биты KBD_MOD_*.
 *
 * Так консольный слой может сам решать, что делать с Alt+F1..F6, и
 * наполнять конкретную виртуальную консоль, а не общую очередь.
 */
typedef void (*keyboard_listener_t)(uint8_t scancode, char c, uint8_t mods);

void keyboard_init(void);
void keyboard_set_listener(keyboard_listener_t listener);

/* Положить символ в поток ввода консоли. Нужно приемнику COM1 (IRQ4):
 * символ уходит тому же приемнику, что и нажатия клавиатуры. */
void keyboard_push_char(char c);
