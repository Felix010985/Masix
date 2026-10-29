/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef VFS_H
#define VFS_H

#include <stdint.h>
#include <stddef.h>
#include <kstat.h>

#define VFS_FILE      1
#define VFS_DIRECTORY 2
#define VFS_SYMLINK   3

#define MAX_FD 32
#define PIPE_COUNT 8
#define PIPE_SIZE 4096
#define UNIX_SOCKET_COUNT 8
#define UNIX_SOCKET_QUEUE 4096
#define UNIX_SOCKET_PATH 108
#define UNIX_SOCKET_NONE 0xFFFFFFFFU
#define INET_SOCKET_COUNT 8
#define INET_DATAGRAM_COUNT 8
#define INET_DATAGRAM_SIZE 512

struct vfs_node;

typedef int32_t (*vfs_read_t)(struct vfs_node *node, uint32_t offset, uint32_t size, uint8_t *buffer);
typedef int32_t (*vfs_write_t)(struct vfs_node *node, uint32_t offset, uint32_t size, const uint8_t *buffer);

typedef struct iovec {
    void* iov_base;
    uint32_t iov_len;
} iovec_t;

typedef struct vfs_node {
    char name[256];
    uint32_t flags;
    uint32_t size;
    int32_t  internal_id;

    vfs_read_t  read;
    vfs_write_t write;
    /* Ссылки: при fork потомок получает те же fd, поэтому узел
     * может использоваться несколькими процессами одновременно.
     * free() только когда refcount == 0. */
    int refcount;
} vfs_node_t;

void vfs_init(void);

int32_t vfs_open(const char *path);
int32_t vfs_open_flags(const char *path, uint32_t flags);
int32_t vfs_read(int fd, void *buf, uint32_t count);
int32_t vfs_write(int fd, const void *buf, uint32_t count);

typedef struct vfs_node vfs_node_t;

/*
 * Регистрация символьных устройств.
 *
 * Драйверы объявляют свои имена устройств и операции при инициализации.
 * VFS и devtmpfs только смотрят эту таблицу и никогда не обращаются к
 * символам драйвера напрямую: зависимость идет от драйвера к VFS,
 * а не наоборот.
 *
 * Имя "console" зарезервировано: устройство, зарегистрировавшее его,
 * обслуживает stdio (fd 0/1/2).
 */
typedef struct {
    const char* name;
    vfs_read_t  read;
    vfs_write_t write;
} vfs_device_t;

void vfs_register_device(const vfs_device_t *dev);
const vfs_device_t* vfs_lookup_device(const char *name);

typedef struct {
    int32_t (*open)(const char *subpath, vfs_node_t *node);
}   fsdriver_t;

typedef struct mountpoint {
    char path[32];               // Точка монтирования
    fsdriver_t *driver;          // Какой драйвер за неё отвечает
    struct mountpoint *next;     // Ссылочка на следующую точку (для списка)
} mountpoint_t;

typedef enum {
    FT_EMPTY = 0,
    FT_VFS_FILE, // Обычный файл
    FT_DEVICE,   // Символьное устройство
    FT_PIPE_READ,
    FT_PIPE_WRITE,
    FT_UNIX_SOCKET,
    FT_INET_SOCKET
} file_type_t;

typedef struct file {
    file_type_t type;    // Тип файла
    uint32_t offset;     // Оффсет чтения/записи
    void* private_data;  // Указатель на vfs_node_t ИЛИ на структуру устройства
} file_t;

/* Таблица дескрипторов текущей задачи. До создания первой задачи
 * (инициализация VFS) возвращается таблица-заглушка ядра. */
file_t* vfs_fd_table(void);

int32_t vfs_close(int fd);
int32_t vfs_pipe(int32_t pipefd[2]);
int32_t vfs_socket(int domain, int type, int protocol);
int32_t vfs_socket_bind(int fd, const void *address, uint32_t address_length);
int32_t vfs_socket_listen(int fd, int backlog);
int32_t vfs_socket_connect(int fd, const void *address, uint32_t address_length);
int32_t vfs_socket_accept(int fd);
int32_t vfs_socket_send(int fd, const void *buffer, uint32_t length);
int32_t vfs_socket_recv(int fd, void *buffer, uint32_t length);
int32_t vfs_socket_sendto(int fd, const void *buffer, uint32_t length, const void *address, uint32_t address_length);
int32_t vfs_socket_recvfrom(int fd, void *buffer, uint32_t length, void *address, uint32_t *address_length);
int32_t vfs_mount(const char *target, fsdriver_t *driver);

#endif
