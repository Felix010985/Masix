/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#include <drivers/pit.h>
#include <drivers/io.h>
#include <drivers/rtl8139.h>
#include <idt.h>
#include <task.h>
#include <stdint.h>



static volatile uint32_t system_ticks = 0;


void pit_handler_c(void) {
    /* ТАК ДЕЛАТЬ НЕЛЬЗЯ
     * но я пока сделаю все равно до 4.0.0 еще далеко
     */
    system_ticks++;
    rtl8139_poll();
}

__attribute__((naked)) void pit_handler_asm(void) {
    __asm__ __volatile__ (
        "pusha \n\t"

        "mov $0x10, %ax \n\t"
        "mov %ax, %ds \n\t"
        "mov %ax, %es \n\t"

        "call pit_handler_c \n\t"

        /* Подтверждаем IRQ0 для PIC ДО task_schedule(): task_schedule() завершается
         * naked iret и не возвращается, поэтому EOI после него
         * никогда бы не выполнился. */
        "mov $0x20, %al \n\t"
        "outb %al, $0x20 \n\t"

        /* Сохраняем ESP в task->esp (поле esp - смещение 8 в task_t:
         * pid=0, brk=4, esp=8). Это адрес pusha-блока текущего
         * прерывания: task_schedule() восстановит его через popa/iret,
         * корректно возобновляя и прерванный код ядра (например,
         * системный вызов, в работе которого сработал таймер). */
        "movl current_task, %eax \n\t"
        "movl %esp, 8(%eax) \n\t"

        "call task_schedule \n\t"

        /* Сюда управление не доходит: task_schedule() не возвращается.
         * Возобновленная задача продолжает исполнение своим popa/iret. */
        "1: \n\t"
        "hlt \n\t"
        "jmp 1b \n\t"
    );
}

void pit_init(uint32_t frequency) {
    uint32_t divisor = PIT_BASE_FREQUENCY / frequency;

    outb(PIT_PORT_COMMAND, 0x36);

    outb(PIT_PORT_DATA0, (uint8_t)(divisor & 0xFF));
    outb(PIT_PORT_DATA0, (uint8_t)((divisor >> 8) & 0xFF));

    idt_register_handler(0x20, (uint32_t)pit_handler_asm, 0x8E);
}

uint32_t pit_get_ticks(void) {
    return system_ticks;
}

void pit_sleep(uint32_t ticks) {
    uint32_t target_ticks = system_ticks + ticks;
    while (system_ticks < target_ticks) {
        asm volatile("hlt");
    }
}
