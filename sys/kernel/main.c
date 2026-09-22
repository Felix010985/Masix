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
#include <drivers/io.h>
#include <filesystem.h>
#include <idt.h>
#include <drivers/pit.h>
#include <panic.h>
#include <drivers/framebuffer.h>
#include <vfs.h>
#include <drivers/network.h>
#include <vmm.h>
#include <task.h>

#include <stdint.h>
#include <stddef.h>
#include <string.h>

uint32_t init_elf_start = 0;
uint32_t init_elf_size = 0;

extern void printf(const char* fmt, ...);
extern void tty_write_char(char c);

void gdt_set_entry(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran);
extern void pic_init(void);
extern void keyboard_init(void);

extern void init_serial(void);
extern void puts_com1(const char* s);

extern void exception_gpf(void);
extern void idt_register_handler(uint8_t vector, uint32_t handler_addr, uint8_t flags);

struct cpio_newc_header {
    char c_magic[6];
    char c_ino[8];
    char c_mode[8];
    char c_uid[8];       // UID
    char c_gid[8];       // GID
    char c_nlink[8];
    char c_mtime[8];
    char c_filesize[8];
    char c_devmajor[8];
    char c_devminor[8];
    char c_rdevmajor[8];
    char c_rdevminor[8];
    char c_namesize[8];
    char c_check[8];
} __attribute__((packed));

static uint32_t parse_hex_ascii(const char* str, size_t len) {
    uint32_t result = 0;
    for (size_t i = 0; i < len; i++) {
        char c = str[i];
        uint32_t val = 0;
        if (c >= '0' && c <= '9') val = c - '0';
        else if (c >= 'a' && c <= 'f') val = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') val = c - 'A' + 10;
        else break;
        result = (result << 4) | val;
    }
    return result;
}

void unpack_initramfs(uint32_t archive_start, uint32_t archive_size) {
    uint8_t* ptr = (uint8_t*)archive_start;
    uint8_t* end = ptr + archive_size;

    while (ptr < end) {
        struct cpio_newc_header* header = (struct cpio_newc_header*)ptr;
        if (memcmp(header->c_magic, "070701", 6) != 0) break;
        /* Парсим заголовок cpio образа виртуального загрузочного диска (initramfs)
         * Если не подходит -- прерываем загрузку
         * После пары проверок прыгаем в распакованный образ сообщая об этом в последовательный порт
         */

        uint32_t namesize = parse_hex_ascii(header->c_namesize, 8);
        uint32_t filesize = parse_hex_ascii(header->c_filesize, 8);
        char* filename = (char*)(ptr + sizeof(struct cpio_newc_header));
        puts_com1("Masix: initramfs found file: ");
        puts_com1(filename);
        puts_com1("\n");

        if (strcmp(filename, "TRAILER!!!") == 0) break;

        uint32_t data_offset = (sizeof(struct cpio_newc_header) + namesize + 3) & ~3;
        uint8_t* file_data = ptr + data_offset;

        if (strcmp(filename, ".") != 0 && strcmp(filename, "..") != 0) {

            char clean_path[256];

            // Если имя в архиве начинается с "./", заменяем "." на "/"
            if (memcmp(filename, "./", 2) == 0) {
                clean_path[0] = '/';
                strncpy(clean_path + 1, filename + 2, sizeof(clean_path) - 2);
            }
            // Если имя идет без точки, принудительно вставляем слэш вперед
            else if (filename[0] != '/') {
                clean_path[0] = '/';
                strncpy(clean_path + 1, filename, sizeof(clean_path) - 2);
            }
            // Если слэш уже есть, копируем как есть
            else {
                strncpy(clean_path, filename, sizeof(clean_path) - 1);
            }

            clean_path[sizeof(clean_path) - 1] = '\0'; // Гарантируем нуль терминатор

            uint32_t mode = parse_hex_ascii(header->c_mode, 8);
            unsigned int type = (mode & 0170000) == 0040000 ? FS_DIRECTORY : FS_REGULAR;
            int idx = fs_create_type(clean_path, type);
            if (idx != -1) {
                /* Регистрируем cpio файл в VFS */
                if (type == FS_REGULAR) fs_attach_external(idx, file_data, filesize);

                puts_com1("Masix: Registered cpio file in VFS: ");
                puts_com1(clean_path);
                puts_com1("\n");
            }
        }

        // Хардкод имени файла /bin/init
        if (strcmp(filename, "./bin/init") == 0 || strcmp(filename, "bin/init") == 0) {
            init_elf_start = (uint32_t)file_data;
            init_elf_size = filesize;
        }

        ptr += data_offset + ((filesize + 3) & ~3);
    }
}

uint32_t initramfs_start = 0;
uint32_t initramfs_size = 0;

uint8_t user_stack[4096];
uint8_t kernel_stack[4096];

struct multiboot_tag {
    uint32_t type;
    uint32_t size;
};

struct multiboot_tag_basic_meminfo {
    uint32_t type;
    uint32_t size;
    uint32_t mem_lower;
    uint32_t mem_upper;
};

struct multiboot_tag_module {
    uint32_t type;
    uint32_t size;
    uint32_t mod_start;
    uint32_t mod_end;
    char string;
};

struct tss_entry_struct {
    uint32_t prev_tss; uint32_t esp0; uint32_t ss0; uint32_t esp1;
    uint32_t ss1; uint32_t esp2; uint32_t ss2; uint32_t cr3;
    uint32_t eip; uint32_t eflags; uint32_t eax; uint32_t ecx;
    uint32_t edx; uint32_t ebx; uint32_t esp; uint32_t ebp;
    uint32_t esi; uint32_t edi; uint32_t es; uint32_t cs;
    uint32_t ss; uint32_t ds; uint32_t fs; uint32_t gs;
    uint32_t ldt; uint16_t trap; uint16_t iomap_base;
} __attribute__((packed));

struct tss_entry_struct tss_entry;

void write_tss(int num, uint16_t ss0, uint32_t esp0) {
    uint32_t base = (uint32_t)&tss_entry;
    uint32_t limit = sizeof(tss_entry) - 1;

    memset(&tss_entry, 0, sizeof(tss_entry));

    tss_entry.ss0 = ss0;
    tss_entry.esp0 = esp0;

    tss_entry.iomap_base = sizeof(tss_entry);

    gdt_set_entry(num, base, limit, 0x89, 0x00);
}

__attribute__((naked)) void jump_to_user(void* shell_ptr, uint32_t user_esp) {
    __asm__ __volatile__(
        "cli \n\t"
        "movl 4(%%esp), %%ebx \n\t"
        "movl 8(%%esp), %%edx \n\t"

        "mov $0x23, %%ax \n\t"
        "mov %%ax, %%ds \n\t"
        "mov %%ax, %%es \n\t"
        "mov %%ax, %%fs \n\t"
        "mov %%ax, %%gs \n\t"
        "pushl $0x23 \n\t"
        "pushl %%edx \n\t"
        "pushl $0x0202 \n\t"
        "pushl $0x1B \n\t"
        "pushl %%ebx \n\t"

        "iret \n\t"
        :
        :
        : "eax", "ebx", "edx", "memory"
    );
}

void kernel_main(uint32_t magic, uint32_t addr) {
    gdt_install();
    idt_install();

    uint32_t cr0;
    uint32_t cr4;


    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1 << 2); // Сбросить EM (Bit 2)
    cr0 |= (1 << 1);  // Установить MP (Bit 1)
    asm volatile("mov %0, %%cr0" : : "r"(cr0));

    asm volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1 << 9);
    cr4 |= (1 << 10);
    asm volatile("mov %0, %%cr4" : : "r"(cr4));

    extern void exception_gpf(void);
    idt_register_handler(13, (uint32_t)exception_gpf, 0x8E);
    extern void exception_div_zero(void);
    idt_register_handler(0, (uint32_t)exception_div_zero, 0x8E);

    // pit_init(100);

    extern void syscall_init(void);
    syscall_init();

    write_tss(5, 0x10, (uint32_t)kernel_stack + 4096);
    asm volatile("ltr %%ax" : : "a"(0x28));

    init_serial();
    puts_com1("COM1 Successfully initialized!\n");
    vfs_init();
    network_init();
    if (network_available()) puts_com1("Masix: RTL8139 network device initialized.\n");

    puts_com1("BEFORE JUMP\n");

    if (magic == 0x36d76289 && addr != 0) {
        uint8_t* tag_ptr = (uint8_t*)(addr + 8);

        while (1) {
            struct multiboot_tag* tag = (struct multiboot_tag*)tag_ptr;

            if (tag->type == 0) break;

            if (tag->type == 4) {
                struct multiboot_tag_basic_meminfo* mem = (struct multiboot_tag_basic_meminfo*)tag;
                extern void pmm_init(uint32_t mem_lower, uint32_t mem_upper);
                pmm_init(mem->mem_lower, mem->mem_upper);
            }

            if (tag->type == 3) {
                struct multiboot_tag_module* mod = (struct multiboot_tag_module*)tag;
                initramfs_start = mod->mod_start;
                initramfs_size = mod->mod_end - mod->mod_start;

                puts_com1("Masix: initramfs module located in memory.\n");
                unpack_initramfs(initramfs_start, initramfs_size);
            }
            if (tag->type == 8) framebuffer_init((struct multiboot_tag_framebuffer*)tag);

            tag_ptr += ((tag->size + 7) & ~7);
        }
    }

    task_init();
    puts_com1("Masix: Task manager initialized.\n");

    /* Прямо используем переменные, которые функция unpack_initramfs только что заполнила */
    if (init_elf_start != 0 && init_elf_size != 0) {

        puts_com1("Masix: Loading ELF binary from initramfs memory...\n");

        extern void* elf_load_binary(uint32_t file_start);
        void* entry_point = elf_load_binary(init_elf_start);

        if (entry_point != NULL) {
            puts_com1("Masix: Creating userland task for init...\n");

            extern void task_create(void* entry_point);
            task_create(entry_point);
        } else {
            puts_com1("Masix: CRITICAL: ELF binary loading failed! Halted.\n");
            for (;;) { asm volatile("hlt"); }
        }
    } else {
        puts_com1("Masix: CRITICAL: init.elf variable is NULL! Halted.\n");
        for (;;) { asm volatile("hlt"); }
    }

    pic_init();
    keyboard_init();

    pit_init(100);
    puts_com1("Masix: PIT Timer registered at 100Hz.\n");

    puts_com1("Masix: Multitasking started! Jumping to Ring 3...\n");
    extern void schedule(void);
    schedule();

    for (;;) {
        puts_com1("Masix: Debug: This message will appear in the serial console if the CPU somehow end here. (Halt NO. 1)\n");
        asm volatile("hlt");
    }
}

