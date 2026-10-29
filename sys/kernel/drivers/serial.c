/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#include <stdint.h>
#include <drivers/io.h>
#include <drivers/serial.h>
#include <drivers/keyboard.h>
#include <idt.h>

#define COM1 0x3F8

/* Смещения регистров 8250 от базы порта. */
#define COM_DATA 0    /* DLAB=0: принятый / передаваемый байт */
#define COM_IER  1    /* DLAB=0: разрешение прерываний */
#define COM_FCR  2    /* управление FIFO */
#define COM_LCR  3    /* параметры кадра, бит DLAB */
#define COM_MCR  4    /* управление модемом */
#define COM_LSR  5    /* состояние линии */

#define COM_LSR_DATA_READY 0x01    /* принят байт */
#define COM_LSR_THR_EMPTY  0x20    /* регистр передачи пуст */

#define COM_IER_RX_AVAILABLE 0x01  /* прерывание "принят байт" */

/* IRQ4 - это вектор 0x24: pic_init() перенастраивает PIC на базу 0x20. */
#define COM1_IRQ_VECTOR 0x24

/* Обработчик прерывания COM1, см. конец файла. */
__attribute__((naked)) void serial_handler_asm(void);

void serial_init(void) {
    outb(COM1 + COM_IER, 0x00);    /* на время настройки прерывания выключены */
    outb(COM1 + COM_LCR, 0x80);    /* DLAB: доступ к делителю частоты */
    outb(COM1 + COM_DATA, 0x03);   /* делитель 3: 38400 бод */
    outb(COM1 + COM_IER, 0x00);
    outb(COM1 + COM_LCR, 0x03);    /* 8 бит, без четности, один стоп-бит */
    outb(COM1 + COM_FCR, 0xC7);    /* FIFO включен и очищен, порог 14 байт */
    outb(COM1 + COM_MCR, 0x0B);    /* DTR, RTS и OUT2: OUT2 пропускает IRQ4 */

    idt_register_handler(COM1_IRQ_VECTOR, (uint32_t)serial_handler_asm, 0x8E);
}

int serial_is_transmit_empty(void) {
    return inb(COM1 + COM_LSR) & COM_LSR_THR_EMPTY;
}

void serial_putc(char a) {
    while (serial_is_transmit_empty() == 0);
    outb(COM1 + COM_DATA, a);
}

void serial_puts(const char* str) {
    for (int i = 0; str[i] != '\0'; i++) {
        serial_putc(str[i]);
    }
}

int serial_received(void) {
    return inb(COM1 + COM_LSR) & COM_LSR_DATA_READY;
}

char serial_getc(void) {
    while (serial_received() == 0);
    return (char)inb(COM1 + COM_DATA);
}

void serial_input_init(void) {
    /* Остатки прошлой жизни порта читаем и выбрасываем, иначе они
     * сразу вызовут прерывание. */
    while (serial_received()) inb(COM1 + COM_DATA);

    outb(COM1 + COM_IER, COM_IER_RX_AVAILABLE);
}

/* Байт пришел по COM1: отдаем его приемнику консоли (тому же, что и
 * клавиатура), то есть активной виртуальной консоли.
 * Возврат каретки терминала приводим к '\n', чтобы Enter с терминала
 * выглядел как Enter с клавиатуры. */
void serial_handler_c(void) {
    while (serial_received()) {
        char c = (char)inb(COM1 + COM_DATA);
        if (c == '\r') c = '\n';
        keyboard_push_char(c);
    }
}

__attribute__((naked)) void serial_handler_asm(void) {
    __asm__ __volatile__ (
        "pusha \n\t"

        "mov $0x20, %al \n\t"
        "out %al, $0x20 \n\t"

        "mov $0x10, %ax \n\t"
        "mov %ax, %ds \n\t"
        "mov %ax, %es \n\t"

        "call serial_handler_c \n\t"

        "popa \n\t"
        "iret"
    );
}

// не готово и будет еще поправлено в будущем, пока вариант от меня и ИИшки от гугла, посмотрим как оно себя поведе
// окей, 06.09.26 и оно готово оказывается, вот так
