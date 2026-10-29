/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <config.h>
#include <filesystem.h>
#include <initramfs.h>

uint32_t initramfs_elf_start = 0;
uint32_t initramfs_elf_size = 0;

uint32_t initramfs_start = 0;
uint32_t initramfs_size = 0;

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

static uint32_t initramfs_parse_hex(const char* str, size_t len) {
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

void initramfs_unpack(uint32_t archive_start, uint32_t archive_size) {
    uint8_t* ptr = (uint8_t*)archive_start;
    uint8_t* end = ptr + archive_size;

    while (ptr < end) {
        struct cpio_newc_header* header = (struct cpio_newc_header*)ptr;
        if (memcmp(header->c_magic, "070701", 6) != 0) break;

        uint32_t namesize = initramfs_parse_hex(header->c_namesize, 8);
        uint32_t filesize = initramfs_parse_hex(header->c_filesize, 8);
        char* filename = (char*)(ptr + sizeof(struct cpio_newc_header));
        debug_puts("Masix: initramfs found file: ");
        debug_puts(filename);
        debug_puts("\n");

        if (strcmp(filename, "TRAILER!!!") == 0) break;

        uint32_t data_offset = (sizeof(struct cpio_newc_header) + namesize + 3) & ~3;
        uint8_t* file_data = ptr + data_offset;

        if (strcmp(filename, "..") != 0) {

            char clean_path[256];

            /* Корень архива (запись ".") регистрируем как "/": без него
             * chdir("/") и разрешение путей в VFS не находят корень. */

            // тут точно где то есть баг, я просто не могу его найти
            // ПОЧТИ НАШЕЛ и опять же чуть не угробил весь масикс
            if (strcmp(filename, ".") == 0) {
                clean_path[0] = '/';
                clean_path[1] = '\0';
            }
            /* Если имя в архиве начинается с "./", заменяем "." на "/" */
            else if (memcmp(filename, "./", 2) == 0) {
                clean_path[0] = '/';
                strncpy(clean_path + 1, filename + 2, sizeof(clean_path) - 2);
            }
            /* Если имя идет без точки, принудительно вставляем слэш вперед */
            else if (filename[0] != '/') {
                clean_path[0] = '/';
                strncpy(clean_path + 1, filename, sizeof(clean_path) - 2);
            }
            /* Если слэш уже есть, копируем как есть */
            else {
                strncpy(clean_path, filename, sizeof(clean_path) - 1);
            }

            clean_path[sizeof(clean_path) - 1] = '\0';

            uint32_t mode = initramfs_parse_hex(header->c_mode, 8);
            unsigned int fmt = mode & 0170000;
            unsigned int type = fmt == 0040000 ? FS_DIRECTORY :
                                fmt == 0120000 ? FS_SYMLINK : FS_REGULAR;
            int idx = fs_create_type(clean_path, type);
            if (idx != -1) {
                /* Регистрируем cpio файл в VFS. Для симлинка external_data
                 * указывает на цель ссылки - VFS разворачивает её сам. */
                if (type == FS_REGULAR || type == FS_SYMLINK)
                    fs_attach_external(idx, file_data, filesize);

                debug_puts("Masix: Registered cpio file in VFS: ");
                debug_puts(clean_path);
                debug_puts("\n");
            }
        }

        /* Хардкод имени файла /bin/init */
        if (strcmp(filename, "./bin/init") == 0 || strcmp(filename, "bin/init") == 0) {
            initramfs_elf_start = (uint32_t)file_data;
            initramfs_elf_size = filesize;
        }

        ptr += data_offset + ((filesize + 3) & ~3);
    }
}
