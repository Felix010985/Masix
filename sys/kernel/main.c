/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#include <gdt.h>
#include <idt.h>
#include <panic.h>
#include <task.h>
#include <tss.h>
#include <initramfs.h>
#include <elf.h>
#include <syscall.h>
#include <boot.h>
#include <pmm.h>
#include <vmm.h>
#include <vfs.h>

#include <drivers/serial.h>
#include <drivers/pit.h>
#include <drivers/pic.h>
#include <config.h>
#include <drivers/keyboard.h>
#include <drivers/framebuffer.h>
#include <drivers/rtl8139.h>
#include <tty.h>

#include <main.h>

#include <linuxkpi/linuxkpi.h>

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* Имя протокола для отладочного вывода. */
static const char* main_protocol_name(enum boot_protocol protocol) {
    switch (protocol) {
        case BOOT_PROTOCOL_LINUX:        return "linux";
        case BOOT_PROTOCOL_LIMINE:        return "limine";
        case BOOT_PROTOCOL_MULTIBOOT2:    return "multiboot2";
        case BOOT_PROTOCOL_MULTIBOOT1:    return "multiboot1";
        default:                          return "none";
    }
}

void main_kernel_main(void) {
    gdt_install();
    idt_install();

    /* Сопроцесс, SSE и страницы настроил бутстрап, здесь это уже готово. */
    idt_register_handler(13, (uint32_t)panic_exception_gpf, 0x8E);
    idt_register_handler(0, (uint32_t)panic_exception_div_zero, 0x8E);
    idt_register_handler(14, (uint32_t)panic_exception_pf, 0x8E);

    syscall_init();

    tss_write(5, 0x10, (uint32_t)tss_kernel_stack + 4096);
    asm volatile("ltr %%ax" : : "a"(0x28));

    /* UART нужен и отладочному выводу, и приему по COM1, поэтому
     * настраиваем его всегда, независимо от выбранного бэкенда. */
    serial_init();

#if defined(DEBUG_OUTPUT_SERIAL) && DEBUG_LEVEL >= DEBUG_LEVEL_INFO
    debug_puts("COM1 Successfully initialized!\n");
#endif

    { extern void itoa(int value, char* str, int base); extern uint32_t boot_pd[1024]; char buf[16];
      debug_puts("boot_pd[768]="); itoa((int)((boot_pd[768]>>16)&0xFFFF),buf,16); debug_puts(buf); debug_puts("_"); itoa((int)(boot_pd[768]&0xFFFF),buf,16); debug_puts(buf);
      debug_puts(" [769]="); itoa((int)((boot_pd[769]>>16)&0xFFFF),buf,16); debug_puts(buf); debug_puts("_"); itoa((int)(boot_pd[769]&0xFFFF),buf,16); debug_puts(buf);
      debug_puts(" [1012]="); itoa((int)((boot_pd[1012]>>16)&0xFFFF),buf,16); debug_puts(buf); debug_puts("_"); itoa((int)(boot_pd[1012]&0xFFFF),buf,16); debug_puts(buf);
      debug_puts("\n"); }
    { extern void itoa(int value, char* str, int base); char buf[16];
      debug_puts("boot protocol: "); debug_puts(main_protocol_name(boot_info.protocol));
      debug_puts(", firmware: "); debug_puts(boot_info.firmware == BOOT_FIRMWARE_EFI ? "efi" : "bios");
      debug_puts(", mem lower/upper KB: "); itoa((int)boot_info.mem_lower, buf, 10); debug_puts(buf);
      debug_puts("/"); itoa((int)boot_info.mem_upper, buf, 10); debug_puts(buf);
      debug_puts(", modules: "); itoa((int)boot_info.module_count, buf, 10); debug_puts(buf);
      debug_puts("\n"); }

    linuxkpi_init();
    vfs_init();
    /* Регистрируем консольное устройство ДО использования stdio,
     * иначе у fd 0/1/2 не будет куда разрешаться. */
    tty_init();
    rtl8139_init();
    if (rtl8139_available()) debug_puts("Masix: RTL8139 network device initialized.\n");

    pmm_init(boot_info.mem_lower, boot_info.mem_upper);

    /* Режим фреймбуфера ядро получило у прошивки еще на этапе загрузки
     * (GOP или VBE), здесь только отображаем видеопамять и запоминаем режим.
     * Если режима нет, консоль остается на текстовом VGA. */
    if (boot_info.framebuffer.valid) {
        if (framebuffer_init(&boot_info.framebuffer) == 0) {
            debug_puts("Masix: linear framebuffer is ready\n");
        } else {
            debug_puts("Masix: linear framebuffer is not usable\n");
        }
    }

    /* initramfs приходит модулем, его загрузчик положил куда ему угодно. */
    for (uint32_t i = 0; i < boot_info.module_count; i++) {
        struct boot_module* mod = &boot_info.modules[i];

                debug_puts("Masix: initramfs module located in memory.\n");
        initramfs_unpack((uint32_t)mod->start, (uint32_t)(mod->end - mod->start));
        break;
    }

    task_init();
    debug_puts("Masix: Task manager initialized.\n");

    if (initramfs_elf_start != 0 && initramfs_elf_size != 0) {

        debug_puts("Masix: Loading ELF binary from initramfs memory...\n");

        void* entry_point = elf_load_binary(initramfs_elf_start);

        if (entry_point != NULL) {
            debug_puts("Masix: Creating userland task for init...\n");

            task_create(entry_point);
        } else {
            debug_puts("Masix: CRITICAL: ELF binary loading failed! Halted.\n");
            for (;;) { asm volatile("hlt"); }
        }
    } else {
        debug_puts("Masix: CRITICAL: init.elf variable is NULL! Halted.\n");
        for (;;) { asm volatile("hlt"); }
    }

    pic_init();
    keyboard_init();

    /* Прием по COM1 включаем только теперь: до pic_init() IRQ4 идет
     * на другой вектор, и прерывание ушло бы не туда. */
    serial_input_init();

    pit_init(100);
    debug_puts("Masix: PIT Timer registered at 100Hz.\n");

    debug_puts("Masix: Multitasking started! Jumping to Ring 3...\n");
    task_schedule();

    for (;;) {
        debug_puts("Masix: Debug: This message will appear in the serial console if the CPU somehow end here. (Halt NO. 1)\n");
        asm volatile("hlt");
    }
}
