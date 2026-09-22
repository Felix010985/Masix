/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#include <api.h>
#include <drivers/vga.h>
#include <drivers/keyboard.h>
#include <alloc.h>
#include <vfs.h>
#include <stdint.h>
#include <string.h>
#include <task.h>

extern void tty_write_char(char c);
extern char keyboard_getc(void);
//extern vfs_node_t* fd_table[];
extern file_t fd_table[32];

int32_t k_sys_write(int fd, const char* buf, uint32_t count) {
    return vfs_write(fd, buf, count);
}

int32_t k_sys_read(int fd, char* buf, uint32_t count) {
    return vfs_read(fd, buf, count);
}

void sys_putc(char c) {
    tty_write_char(c);
}

void sys_cls(void) {
    k_sys_write(1, "\033[2J\033[H", 7);
}

extern task_t* current_task;

int32_t sys_chdir(const char *path) {
    if (!path) return -14; /* -EFAULT: Битый указатель на строку пути */

        if (!current_task) return -1;

        char new_path[MAX_PATH];
    memset(new_path, 0, MAX_PATH);

    /* Нормализация пути: проверяем относительный путь или абсолютный */
    if (path[0] == '/') {
        /* Если путь начинается с '/' - копируем как есть */
        strncpy(new_path, path, MAX_PATH - 1);
    } else {
        /* Если путь относительный - склеиваем его с текущей директорией CWD текущего процесса */
        strncpy(new_path, current_task->cwd, MAX_PATH - 1);

        /* Добавляем слеш в конец текущего CWD, если его там нет */
        uint32_t len = strlen(new_path);
        if (len > 0 && new_path[len - 1] != '/') {
            strncat(new_path, "/", MAX_PATH - len - 1);
        }

        /* Приклеиваем относительный путь, запрошенный процессом */
        strncat(new_path, path, MAX_PATH - strlen(new_path) - 1);
    }

    /* Используем VFS: пытаемся открыть путь, чтобы проверить его существование */
    int32_t fd = vfs_open(new_path);
    if (fd < 0) {
        return -2; /* -ENOENT: Такой папки или пути вообще не существует на диске */
    }

    /* Извлекаем vfs_node_t из fd_table, чтобы проверить тип файла */
    file_t* file_desc = &fd_table[fd];
    vfs_node_t* node = (vfs_node_t*)file_desc->private_data;

    if (!node || node->flags != VFS_DIRECTORY) {
        vfs_close(fd); /* Обязательно закрываем fd, чтобы не упереться в лимит 32 дескриптора */
        return -20;    /* -ENOTDIR: Путь существует, но это обычный файл, а не папка, это qчень важно,
        чтобы не происходило повреждение файлов в случае ошибки */
    }

    /* Путь проверен, это реально директория. Закрываем временный fd */
    vfs_close(fd);

    /* Обновляем рабочую директорию процесса */
    strncpy(current_task->cwd, new_path, MAX_PATH - 1);

    return 0;
}

extern fsdriver_t procfs_driver;
extern fsdriver_t devtmpfs_driver;

int32_t sys_mount(const char *source, const char *target, const char *filesystemtype, unsigned long flags, const void *data) {
    if (!target || !filesystemtype) {
        return -1;
    }

    fsdriver_t *selected_driver = NULL;
    /* Находим какую именно файловую систему пытались примонтировать, и
     * обозначаем драйвер для соответствующей
     */
    if (strcmp(filesystemtype, "proc") == 0) {
        selected_driver = &procfs_driver;
    } else if (strcmp(filesystemtype, "devtmpfs") == 0) {
        selected_driver = &devtmpfs_driver;
    } else {
        return -2; // ENODEV
    }

    return vfs_mount(target, selected_driver);
}

int32_t sys_dup2(int32_t oldfd, int32_t newfd) {
    if (oldfd < 0 || oldfd >= MAX_FD || newfd < 0 || newfd >= MAX_FD) {
        return -9; // -EBADF
    }

    if (fd_table[oldfd].type == FT_EMPTY) {
        return -9; // -EBADF
    }

    if (oldfd == newfd) {
        return newfd;
    }

    if (fd_table[newfd].type != FT_EMPTY) {
        vfs_close(newfd);
        // Или:
        // fd_table[newfd].type = FT_EMPTY;
    }

    fd_table[newfd].type = fd_table[oldfd].type;
    fd_table[newfd].offset = fd_table[oldfd].offset;
    fd_table[newfd].private_data = fd_table[oldfd].private_data;

    return newfd;
}

void* malloc(size_t size) {
    return alloc(size);
}

void free(void* ptr) {
    alloc_free(ptr);
}
