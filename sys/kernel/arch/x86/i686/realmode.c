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
#include <string.h>

#include <boot.h>
#include <config.h>
#include <gdt.h>
#include <realmode.h>

/* Вызов BIOS из 32-битного защищенного режима без помощи загрузчика.
 * Файл специфичен для i686: из 64-битного режима в реальный режим
 * не перейти одним шагом, поэтому для x86-64 появится отдельный файл
 * того же назначения.
 */

/* Рабочая страница в нижней памяти. Обращения внутри станка идут
 * 16-битными смещениями при базе сегмента 0, поэтому страница обязана
 * лежать ниже 64 КБ. Вся страница лежит ниже традиционной границы 0x7C00,
 * чтобы не задеть ни загрузочный сектор, ни область данных BIOS. */
#define REALMODE_SCRATCH 0x7000

#define REALMODE_STUB_OFF   0x0500    /* станок */
#define REALMODE_GDT_OFF    0x0000    /* временный GDT, 4 записи по 8 байт */
#define REALMODE_GDTR_OFF   0x0020    /* псевдодескриптор GDTR */
#define REALMODE_REGS_OFF   0x0040    /* образ регистров для INT 10h */
#define REALMODE_FARPTR_OFF 0x0060    /* адрес возврата станка */
#define REALMODE_SGDT_OFF   0x0080    /* спасенный GDTR ядра */
#define REALMODE_CR0_OFF    0x0090    /* спасенное значение CR0 */
#define REALMODE_CR3_OFF    0x00C0    /* спасенное значение CR3 */
#define REALMODE_CR4_OFF    0x00D0    /* спасенное значение CR4 */
#define REALMODE_ESP_OFF    0x00E0    /* спасенная вершина стека ядра */
#define REALMODE_IDT_OFF    0x00A0    /* псевдодескриптор IDT для реального режима */
#define REALMODE_STACK_TOP  0x5000    /* вершина стека реального режима */

/* Селекторы временного GDT. Селектор 16-битного кода обязан равняться
 * базе сегмента, поделенной на 16: реальный режим считает базу сегмента
 * как sel * 16, и именно так BIOS возвращается из прерывания командой iret.
 * Поэтому дескриптор кода 16 бит строится с базой REALMODE_CODE16_BASE,
 * а вход в станок идет по смещению REALMODE_STUB_ENTER. */
#define REALMODE_SEL_NULL   0x00
#define REALMODE_SEL_CODE16 0x08
#define REALMODE_SEL_DATA16 0x10
#define REALMODE_SEL_CODE32 0x18

/* Селекторы ядра, их задает gdt_install(). */
#define KERNEL_SEL_CODE 0x08
#define KERNEL_SEL_DATA 0x10

/* База сегмента 16-битного кода и смещение входа в станок: станок лежит
 * по линейному адресу REALMODE_SCRATCH + REALMODE_STUB_OFF, значит смещение
 * входа на величину базы меньше. */
#define REALMODE_CODE16_BASE (REALMODE_SEL_CODE16 << 4)
#define REALMODE_STUB_ENTER  (REALMODE_SCRATCH + REALMODE_STUB_OFF - REALMODE_CODE16_BASE)

/* Подстановка значения в строку ассемблерной вставки. */
#define REALMODE_STR2(x) #x
#define REALMODE_STR(x)  REALMODE_STR2(x)

/* 16-битный станок. Вход выполняется дальним переходом из 32-битного кода,
 * при этом база CS равна REALMODE_CODE16_BASE, а страницы уже выключены.
 * База выбрана по правилу реального режима (sel * 16), поэтому возврат
 * BIOS из INT 10h командой iret попадает точно на следующую команду.
 * Станок сам снимает
 * бит PE, то есть переходит в реальный режим, выполняет INT 10h по образу
 * регистров и возвращается в ядро дальним переходом через указатель
 * в рабочей странице. Все команды стандартны для i686, байты собраны
 * по кодовому представлению и сверены дизассемблированием. */
static const uint8_t realmode_stub[] = {
    0xFA, 0xFC, 0x0F, 0x20, 0xC0, 0x83, 0xE0, 0xFE, 0x0F, 0x22, 0xC0, 0x31, 0xC0, 0x8E, 0xD8, 0x8E, 0xC0, 0x8E, 0xD0, 0x66, 0xBC, 0x00, 0x50, 0x00, 0x00, 0xC7, 0x06, 0xA0, 0x70, 0xFF, 0x03, 0x66, 0xC7, 0x06, 0xA2, 0x70, 0x00, 0x00, 0x00, 0x00, 0x0F, 0x01, 0x1E, 0xA0, 0x70, 0x9C, 0x60, 0xBD, 0x40, 0x70, 0x8B, 0x46, 0x0E, 0x8E, 0xC0, 0x8B, 0x46, 0x00, 0x8B, 0x5E, 0x02, 0x8B, 0x4E, 0x04, 0x8B, 0x56, 0x06, 0x8B, 0x76, 0x08, 0x8B, 0x7E, 0x0A, 0xCD, 0x10, 0xBD, 0x40, 0x70, 0x89, 0x46, 0x00, 0x89, 0x5E, 0x02, 0x89, 0x4E, 0x04, 0x89, 0x56, 0x06, 0x89, 0x76, 0x08, 0x89, 0x7E, 0x0A, 0x8C, 0xC0, 0x89, 0x46, 0x0E, 0x61, 0x9D, 0x0F, 0x20, 0xC0, 0x83, 0xC8, 0x01, 0x0F, 0x22, 0xC0, 0x66, 0xFF, 0x2E, 0x60, 0x70
};

/* Регистры для INT 10h объявлены в <realmode.h>: их читает 16-битный станок. */

/* Псевдодескриптор GDTR: предел 16 бит, база 32 бита. */
struct realmode_gdtr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* Указатель дальнего перехода: смещение 32 бита, селектор 16 бит. */
struct realmode_farptr {
    uint32_t offset;
    uint16_t selector;
} __attribute__((packed));

/* Обращение к полям рабочей страницы по фиксированным физическим адресам.
 * Страница отображена тождественно и при включенных страницах, и без них,
 * поэтому такие обращения корректны в любой момент перехода. */
#define REALMODE_U16(off) (*(volatile uint16_t *)(uintptr_t)(REALMODE_SCRATCH + (off)))
#define REALMODE_U32(off) (*(volatile uint32_t *)(uintptr_t)(REALMODE_SCRATCH + (off)))

#define REALMODE_GDTR (*(volatile struct realmode_gdtr *)(uintptr_t)(REALMODE_SCRATCH + REALMODE_GDTR_OFF))
#define REALMODE_SGDT (*(volatile struct realmode_gdtr *)(uintptr_t)(REALMODE_SCRATCH + REALMODE_SGDT_OFF))
#define REALMODE_FAR1 (*(volatile struct realmode_farptr *)(uintptr_t)(REALMODE_SCRATCH + REALMODE_FARPTR_OFF))
#define REALMODE_FAR2 (*(volatile struct realmode_farptr *)(uintptr_t)(REALMODE_SCRATCH + REALMODE_FARPTR_OFF + 8))

/* Запись временного GDT: 8 байт в стандартном порядке:
 * предел 16 бит, база 16 бит, база 8 бит, права, байт гранулярности,
 * база 8 бит. Базу задает вызывающий: у сегментов данных она нулевая,
 * у кода 16 бит равна REALMODE_CODE16_BASE. */
static void realmode_gdt_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    uint8_t* e = (uint8_t*)(uintptr_t)(REALMODE_SCRATCH + REALMODE_GDT_OFF + index * 8);

    e[0] = (uint8_t)(limit & 0xFF);
    e[1] = (uint8_t)((limit >> 8) & 0xFF);
    e[2] = (uint8_t)(base & 0xFF);             /* база, младшие 16 бит */
    e[3] = (uint8_t)((base >> 8) & 0xFF);
    e[4] = (uint8_t)((base >> 16) & 0xFF);     /* база, средний байт */
    e[5] = access;
    e[6] = (uint8_t)(((limit >> 16) & 0x0F) | (gran & 0xF0));
    e[7] = (uint8_t)((base >> 24) & 0xFF);     /* база, старший байт */
}

/* Подготовить рабочую страницу: скопировать станок, собрать временный GDT
 * и псевдодескриптор GDTR. */
static void realmode_scratch_prepare(void) {
    memcpy((void*)(uintptr_t)(REALMODE_SCRATCH + REALMODE_STUB_OFF),
           realmode_stub, sizeof(realmode_stub));

    /* Предел 16-битных сегментов равен 1 МБ (0xFFFFF) при бйтовой гранулярности,
     * ровно как у LINUX и Limine: за это границу реальный режим не видит.
     * База кода 16 бит не нулевая: реальный режим считает базу как sel * 16. */
    realmode_gdt_entry(0, 0, 0, 0, 0);                                       /* null */
    realmode_gdt_entry(REALMODE_SEL_CODE16 / 8, REALMODE_CODE16_BASE, 0xFFFFF, 0x9A, 0x00);  /* код 16 бит */
    realmode_gdt_entry(REALMODE_SEL_DATA16 / 8, 0, 0xFFFFF, 0x92, 0x00);     /* данные 16 бит */
    realmode_gdt_entry(REALMODE_SEL_CODE32 / 8, 0, 0xFFFFFFFF, 0x9A, 0xCF);  /* код 32 бита */

    REALMODE_GDTR.limit = (uint16_t)(4 * 8 - 1);
    REALMODE_GDTR.base = REALMODE_SCRATCH + REALMODE_GDT_OFF;
}

/* Выполнить INT 10h с заданными регистрами.
 * Лежит в секции загрузки: ее адрес равен физическому, потому что во время
 * вызова страницы выключаются и дальний переход возврата идет по адресу
 * метки из этой функции. */
__attribute__((section(".text.boot")))
void realmode_int10(struct realmode_regs* regs) {
    realmode_scratch_prepare();

    /* Образ регистров для станка. */
    REALMODE_U16(REALMODE_REGS_OFF + 0) = regs->ax;
    REALMODE_U16(REALMODE_REGS_OFF + 2) = regs->bx;
    REALMODE_U16(REALMODE_REGS_OFF + 4) = regs->cx;
    REALMODE_U16(REALMODE_REGS_OFF + 6) = regs->dx;
    REALMODE_U16(REALMODE_REGS_OFF + 8) = regs->si;
    REALMODE_U16(REALMODE_REGS_OFF + 10) = regs->di;
    REALMODE_U16(REALMODE_REGS_OFF + 12) = 0;
    REALMODE_U16(REALMODE_REGS_OFF + 14) = regs->es;

    /* Селекторы передаем числами через входы ассемблерной вставки, чтобы
     * десятичная запись не превратилась в чужой дескриптор. */
    __asm__ volatile (
        "cli\n\t"
        "sgdt %[sgdt]\n\t"                        /* спасти GDTR ядра */
        "lgdt %[temp]\n\t"                        /* поставить временный GDT */
        "mov %%cr0, %%eax\n\t"
        "movl %%eax, %[cr0]\n\t"                  /* спасти CR0 */
        "andl $0x7fffffff, %%eax\n\t"             /* выключить страницы, PE оставить */
        "movl %%eax, %%cr0\n\t"
        /* BIOS отдаем чистое состояние: без страниц, без CR3, без CR4. */
        "movl %%cr3, %%eax\n\t"
        "movl %%eax, %[cr3]\n\t"
        "xorl %%eax, %%eax\n\t"
        "movl %%eax, %%cr3\n\t"
        "movl %%cr4, %%eax\n\t"
        "movl %%eax, %[cr4]\n\t"
        "xorl %%eax, %%eax\n\t"
        "movl %%eax, %%cr4\n\t"
        /* Переключаем сегменты данных на 16-битные дескрипторы, пока еще
         * включен защищенный режим: только загрузка сегмента в защищенном
         * режиме обновляет кэш дескриптора, а в реальном режиме обновляется
         * лишь база. Иначе в кэше остается 32-битный дескриптор ядра. */
        "mov %[seld16], %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%ss\n\t"
        /* Стек станка лежит в нижней памяти, стек ядра туда не достает:
         * спасаем вершину стека ядра и вернем ее по возвращении. */
        "movl %%esp, %[esp]\n\t"
        "movl $0x5000, %%esp\n\t"
        "leal 1f, %%eax\n\t"                      /* адрес возврата станка */
        "movl %%eax, %[far1off]\n\t"
        "movw %[sel32], %[far1sel]\n\t"           /* код 32 бита из временного GDT */
        "leal 2f, %%eax\n\t"                      /* адрес возврата в ядро */
        "movl %%eax, %[far2off]\n\t"
        "movw %[selk], %[far2sel]\n\t"             /* селектор кода ядра */
        "ljmp $" REALMODE_STR(REALMODE_SEL_CODE16) ", $" REALMODE_STR(REALMODE_STUB_ENTER) "\n\t" /* уход в 16-битный станок */
        "1:\n\t"
        /* Станок вернулся: PE снова включен, страницы еще выключены. */
        "mov %[esp], %%esp\n\t"                    /* вернуть стек ядра */
        "lgdt %[sgdt]\n\t"                        /* вернуть GDTR ядра */
        /* Порядок обязателен: CR4 и CR3 ставятся до включения страниц
         * в CR0, иначе первая же выборка команды пойдет по пустому
         * каталогу страниц (CR3 = 0 в этот момент). */
        "mov %[cr4], %%eax\n\t"
        "movl %%eax, %%cr4\n\t"                    /* вернуть CR4 */
        "mov %[cr3], %%eax\n\t"
        "movl %%eax, %%cr3\n\t"                    /* вернуть каталог страниц */
        "mov %[cr0], %%eax\n\t"
        "movl %%eax, %%cr0\n\t"                   /* вернуть состояние страниц */
        "mov %[seld], %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%ss\n\t"
        "ljmp *%[far2ptr]\n\t"                     /* вернуть CS ядру */
        "2:\n\t"
        :
        : [sgdt] "m"(REALMODE_SGDT),
          [temp] "m"(REALMODE_GDTR),
          [cr0] "m"(REALMODE_U32(REALMODE_CR0_OFF)),
          [cr3] "m"(REALMODE_U32(REALMODE_CR3_OFF)),
          [cr4] "m"(REALMODE_U32(REALMODE_CR4_OFF)),
          [esp] "m"(REALMODE_U32(REALMODE_ESP_OFF)),
          [far1off] "m"(REALMODE_FAR1.offset),
          [far1sel] "m"(REALMODE_FAR1.selector),
          [far2off] "m"(REALMODE_FAR2.offset),
          [far2sel] "m"(REALMODE_FAR2.selector),
          [far2ptr] "m"(REALMODE_FAR2),
          [seld16] "i"(REALMODE_SEL_DATA16),
          [sel32] "i"(REALMODE_SEL_CODE32),
          [selk] "i"(KERNEL_SEL_CODE),
          [seld] "i"(KERNEL_SEL_DATA)
        : "eax", "memory"
    );

    regs->ax = REALMODE_U16(REALMODE_REGS_OFF + 0);
    regs->bx = REALMODE_U16(REALMODE_REGS_OFF + 2);
    regs->cx = REALMODE_U16(REALMODE_REGS_OFF + 4);
    regs->dx = REALMODE_U16(REALMODE_REGS_OFF + 6);
    regs->si = REALMODE_U16(REALMODE_REGS_OFF + 8);
    regs->di = REALMODE_U16(REALMODE_REGS_OFF + 10);
    regs->es = REALMODE_U16(REALMODE_REGS_OFF + 14);
}
