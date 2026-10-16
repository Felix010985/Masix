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
#include <stddef.h>

#include <arch_boot.h>
#include <boot.h>
#include <config.h>
#include <gdt.h>
#include <main.h>
#include <boot/multiboot2.h>

/* Символы скрипта линковки. */
extern uint8_t _bss_start;
extern uint8_t _bss_end;

/* База, по которой слинковано ядро. Значение совпадает с linker.ld. */
#define KERNEL_LINK_BASE 0xC0000000

/* Каталог страниц этапа загрузки и его стек.
 * Оба лежат в низкой области, их виртуальные адреса равны физическим,
 * поэтому они доступны и при выключенных страницах. */
__attribute__((section(".bss.boot"), aligned(4096)))
uint32_t boot_pd[1024];

__attribute__((section(".bss.boot"), aligned(16)))
uint8_t boot_stack[4096 * 4];

/* Точка входа образа. Ее используют три протокола:
 *   - заголовок ELF (e_entry) и тег entry address заголовка multiboot2 (GRUB),
 *   - поле code32_start заголовка setup (протокол LINUX, GRUB и Limine),
 *   - переход Limine на адрес загрузки ядра.
 * Все три входят в 32-битный защищенный режим с выключенными страницами,
 * код при этом лежит по своим физическим адресам.
 * Поэтому функция и все, что вызывает она до включения страниц,
 * лежат в секции .text.boot. */
__asm__(".globl entry_start");
__attribute__((section(".text.entry"), naked))
void entry_start(void) {
    __asm__ volatile(
        "cli\n\t"
        /* Свой стек вместо неизвестного стека загрузчика. */
        "movl $(boot_stack + 16384), %%esp\n\t"
        /* Регистры протоколов сохраняем сразу, пока они не потерялись.
         * multiboot2: EAX магия, EBX адрес информационной структуры.
         * протокол LINUX: ESI адрес нулевой страницы. */
        "pushl %%esi\n\t"
        "pushl %%ebx\n\t"
        "pushl %%eax\n\t"
        "call entry_boot_stage\n\t"
        "1:\n\t"
        "hlt\n\t"
        "jmp 1b\n\t"
        :
        :
        : "memory"
    );
}

/* Включить свои таблицы страниц: тождественное отображение нижних 3 ГБ
 * и ядро в верхней половине, как у LINUX на i386 (PAGE_OFFSET 0xC0000000).
 * Оставшиеся записи каталога (0xE0000000 и выше) оставлены свободными:
 * там драйверы отображают видеопамять, см. vmm_map_mmio().
 * Функция обязана выполняться по физическому адресу, потому что ее вызывает
 * код с выключенными страницами. */
__attribute__((section(".text.boot")))
static void entry_paging_enable(void) {
    /* 4 МБ страницы: present, write, PSE. */
    /* Тождественно отображаем всю память до 3 ГБ: загрузчик кладет данные
     * туда, где ему удобно, а протокол LINUX (GRUB, Limine) грузит
     * initramfs высоко. У Multiboot2 модуль лежит в первых мегабайтах,
     * поэтому раньше хватало 512 МБ, и на больших машинах ядро падало
     * уже на разборе initramfs. */
    for (uint32_t i = 0; i < 768; i++) {
        boot_pd[i] = (i << 22) | 0x87;
    }
    /* Ядро в верхней половине: 0xC0000000 + физический адрес, то есть
     * физические 0..512 МБ. */
    for (uint32_t i = 0; i < 128; i++) {
        boot_pd[768 + i] = (i << 22) | 0x87;
    }

    __asm__ volatile(
        "mov %0, %%cr3\n\t"
        "mov %%cr4, %%eax\n\t"
        "or $0x10, %%eax\n\t"          /* CR4.PSE */
        "mov %%eax, %%cr4\n\t"
        "mov %%cr0, %%eax\n\t"
        "or $0x80000000, %%eax\n\t"    /* CR0.PG */
        "mov %%eax, %%cr0\n\t"
        : : "r"((uint32_t)(uintptr_t)boot_pd) : "eax", "memory"
    );
}

/* Включить сопроцесс и SSE. Делать это надо до любого кода на C, потому что
 * компилятор пользуется SSE для копирования структур, а без OSFXSR
 * такие команды дают необработанное исключение. */
static void entry_fpu_enable(void) {
    __asm__ volatile(
        "mov %%cr4, %%eax\n\t"
        "or $0x600, %%eax\n\t"         /* OSFXSR и OSXMMEXCPT */
        "mov %%eax, %%cr4\n\t"
        "mov %%cr0, %%eax\n\t"
        "and $0xfffffffb, %%eax\n\t"   /* сбросить EM */
        "or $0x2, %%eax\n\t"           /* установить MP */
        "mov %%eax, %%cr0\n\t"
        : : : "eax", "memory"
    );
}

/* Обнулить BSS ядра. Загрузчик не обнуляет память за концом файла образа,
 * а в BSS лежат все глобальные переменные, включая struct boot_info. */
static void entry_bss_clear(void) {
    for (uint8_t* p = &_bss_start; p < &_bss_end; p++) {
        *p = 0;
    }
}

/* Собрать сведения о машине от того протокола, которым загрузились. */
static void entry_gather(uint32_t magic, uint32_t addr, uint32_t linux_params) {
    if (magic == MULTIBOOT2_MAGIC) {
        multiboot2_gather(magic, addr);
        return;
    }

    /* multiboot2 не сработал: значит вошли по протоколу LINUX, где адрес
     * нулевой страницы приходит в ESI. Признак протокола проверяет сам
     * разбор по подписи "HdrS" в заголовке setup. */
    linux_gather(linux_params);
}

/* Общий финал загрузки для всех протоколов.
 * Фреймбуфер к этому моменту уже запрошен у прошивки (GOP или VBE). */
static void entry_run_kernel(void) {
    main_kernel_main();
}

/* Путь загрузки для протоколов с плоскими адресами: multiboot2 и LINUX.
 * Выполняется по физическому адресу, потому что внутри вызывается
 * переход в реальный режим (VBE). */
__attribute__((section(".text.boot")))
void entry_boot_stage(uint32_t magic, uint32_t addr, uint32_t linux_params) {
    entry_paging_enable();
    entry_fpu_enable();
    entry_bss_clear();
    gdt_install();

    entry_gather(magic, addr, linux_params);

    /* Фреймбуфер просим у прошивки: GOP (EFI) или VBE (BIOS).
     * Загрузчик в этом не участвует. */
    framebuffer_request_dispatch(&boot_info.framebuffer);

    entry_run_kernel();
}

/* Продолжение пути Limine, см. entry_limine_start. */
void entry_limine_boot(void);

/* Точка входа протокола Limine. Протокол требует ядро в верхней половине,
 * поэтому адрес этой функции отдается загрузчику запросом Entry Point
 * (файл arch/x86/limine/limine.c). Вход: 32-битный защищенный режим,
 * страницы включены таблицами Limine, ядро отображено в верхней половине. */
void entry_limine_start(void) {
    /* Данные Limine читаем сразу, пока живы его таблицы страниц. */
    limine_gather();

    /* Дальше общий путь: свои таблицы, свои регистры, свой фреймбуфер. */
    entry_limine_boot();
}

/* Продолжение пути Limine после сбора данных.
 * Тоже выполняется по физическому адресу, см. entry_boot_stage. */
__attribute__((section(".text.boot")))
void entry_limine_boot(void) {
    entry_paging_enable();
    entry_fpu_enable();
    entry_bss_clear();
    gdt_install();

    /* Фреймбуфер просим у прошивки: GOP (EFI) или VBE (BIOS). */
    framebuffer_request_dispatch(&boot_info.framebuffer);
    entry_run_kernel();
}
