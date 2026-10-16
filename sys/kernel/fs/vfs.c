/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#include <vfs.h>
#include <filesystem.h>
#include <alloc.h>
#include <string.h>
#include <stdio.h>
#include <version.h>
#include <procfs.h>
#include <drivers/serial.h>

extern fsdriver_t procfs_driver;
extern fsdriver_t devtmpfs_driver;

mountpoint_t *mountpoints_list = NULL;
file_t fd_table[32];

/* --- Реестр символьных устройств --------------------------------- */

#define MAX_DEVICES 8
static vfs_device_t device_table[MAX_DEVICES];
static int device_count = 0;

/* stdio (fd 0/1/2). Операции пустые, пока драйвер не зарегистрирует
 * зарезервированное имя "console". VFS только хранит указатели,
 * сам ввод-вывод не выполняет.
 *
 * Регистрация должна произойти после vfs_init() и до запуска
 * юзерспейса, т.к. fd 0/1/2 ссылаются на этот узел.
 */
static vfs_node_t console_node = {
    .name = "console",
    .flags = VFS_FILE,
    .size = 0,
    .internal_id = -1,
    .read = NULL,
    .write = NULL
};

void vfs_register_device(const vfs_device_t *dev) {
    if (!dev || !dev->name || device_count >= MAX_DEVICES) return;

    int slot = -1;
    for (int i = 0; i < device_count; i++) {
        if (strcmp(device_table[i].name, dev->name) == 0) { slot = i; break; }
    }
    if (slot < 0) slot = device_count++;
    device_table[slot] = *dev;

    /* stdio связывается напрямую с драйвером, занявшим имя "console" */
    if (strcmp(dev->name, "console") == 0) {
        console_node.read = dev->read;
        console_node.write = dev->write;
    }
}

const vfs_device_t* vfs_lookup_device(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < device_count; i++) {
        if (strcmp(device_table[i].name, name) == 0) return &device_table[i];
    }
    return NULL;
}

typedef struct {
    uint8_t data[PIPE_SIZE];
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count;
    uint32_t readers;
    uint32_t writers;
    int used;
} pipe_t;

static pipe_t pipes[PIPE_COUNT];

typedef struct unix_socket {
    uint8_t data[UNIX_SOCKET_QUEUE];
    uint32_t read_pos;
    uint32_t write_pos;
    uint32_t count;
    uint32_t peer;
    uint32_t pending;
    int used;
    int listening;
    int connected;
    char path[UNIX_SOCKET_PATH];
} unix_socket_t;

static unix_socket_t unix_sockets[UNIX_SOCKET_COUNT];

typedef struct {
    uint8_t data[INET_DATAGRAM_SIZE];
    uint32_t length;
    uint32_t source_address;
    uint16_t source_port;
} inet_datagram_t;

typedef struct {
    inet_datagram_t datagrams[INET_DATAGRAM_COUNT];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint32_t address;
    uint16_t port;
    uint16_t peer_port;
    uint32_t peer_address;
    int used;
    int connected;
} inet_socket_t;

static inet_socket_t inet_sockets[INET_SOCKET_COUNT];

static int32_t vfs_allocate_fd(file_type_t type, void *private_data) {
    for (int i = 3; i < MAX_FD; i++) {
        if (fd_table[i].type == FT_EMPTY) {
            fd_table[i].type = type;
            fd_table[i].offset = 0;
            fd_table[i].private_data = private_data;
            return i;
        }
    }
    return -24;
}

static int32_t vfs_pipe_read(pipe_t *pipe, uint8_t *buffer, uint32_t size) {
    uint32_t read = 0;
    while (read < size && pipe->count > 0) {
        buffer[read++] = pipe->data[pipe->read_pos];
        pipe->read_pos = (pipe->read_pos + 1) % PIPE_SIZE;
        pipe->count--;
    }
    return read;
}

static int32_t vfs_pipe_write(pipe_t *pipe, const uint8_t *buffer, uint32_t size) {
    if (pipe->readers == 0) return -32;
    uint32_t written = 0;
    while (written < size && pipe->count < PIPE_SIZE) {
        pipe->data[pipe->write_pos] = buffer[written++];
        pipe->write_pos = (pipe->write_pos + 1) % PIPE_SIZE;
        pipe->count++;
    }
    return written;
}

static int32_t vfs_socket_queue_write(unix_socket_t *socket, const uint8_t *buffer, uint32_t size) {
    if (!socket->connected) return -107;
    uint32_t written = 0;
    while (written < size && socket->count < UNIX_SOCKET_QUEUE) {
        socket->data[socket->write_pos] = buffer[written++];
        socket->write_pos = (socket->write_pos + 1) % UNIX_SOCKET_QUEUE;
        socket->count++;
    }
    return written;
}

static int32_t vfs_socket_queue_read(unix_socket_t *socket, uint8_t *buffer, uint32_t size) {
    uint32_t read = 0;
    while (read < size && socket->count > 0) {
        buffer[read++] = socket->data[socket->read_pos];
        socket->read_pos = (socket->read_pos + 1) % UNIX_SOCKET_QUEUE;
        socket->count--;
    }
    return read;
}

struct inet_sockaddr {
    uint16_t family;
    uint16_t port;
    uint32_t address;
    uint8_t padding[8];
};

static uint16_t vfs_network_port(uint16_t port) { return (uint16_t)((port >> 8) | (port << 8)); }

static int32_t vfs_inet_address(const void *address, uint32_t address_length, uint32_t *ip, uint16_t *port) {
    if (!address || address_length < 8) return -14;
    const struct inet_sockaddr *socket_address = (const struct inet_sockaddr*)address;
    if (socket_address->family != 2) return -97;
    *ip = socket_address->address;
    *port = vfs_network_port(socket_address->port);
    if (*ip != 0 && *ip != 0x0100007F) return -101;
    if (*port == 0) return -22;
    return 0;
}

static inet_socket_t *vfs_inet_socket_from_fd(int fd) {
    if (fd < 0 || fd >= MAX_FD || fd_table[fd].type != FT_INET_SOCKET) return NULL;
    return (inet_socket_t*)fd_table[fd].private_data;
}

static int32_t vfs_inet_send(inet_socket_t *source, const uint8_t *buffer, uint32_t length, uint32_t address, uint16_t port) {
    if (length > INET_DATAGRAM_SIZE) return -90;
    for (int i = 0; i < INET_SOCKET_COUNT; i++) {
        inet_socket_t *destination = &inet_sockets[i];
        if (!destination->used || destination->port != port) continue;
        if (destination->address != 0 && destination->address != address) continue;
        if (destination->count == INET_DATAGRAM_COUNT) return -11;
        inet_datagram_t *datagram = &destination->datagrams[destination->tail];
        for (uint32_t j = 0; j < length; j++) datagram->data[j] = buffer[j];
        datagram->length = length;
        datagram->source_address = source->address ? source->address : 0x0100007F;
        datagram->source_port = source->port;
        destination->tail = (destination->tail + 1) % INET_DATAGRAM_COUNT;
        destination->count++;
        return length;
    }
    return -111;
}

static int32_t vfs_ramfs_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer) {
    return fs_read_at(node->internal_id, offset, buffer, size);
}

static int32_t vfs_ramfs_write(vfs_node_t *node, uint32_t offset, uint32_t size, const uint8_t *buffer) {
    (void)offset;
    int32_t ret = fs_write(node->internal_id, buffer, size);
    node->size = file_table[node->internal_id].size;
    return ret;
}

void vfs_mount_core(const char *path, fsdriver_t *driver) {
    /* Монтаж корня (/) и аллокация памяти к нему */
    mountpoint_t *mp = (mountpoint_t*)malloc(sizeof(mountpoint_t));
    if (!mp) return;

    strncpy(mp->path, path, 32);
    mp->driver = driver;
    mp->next = mountpoints_list;
    mountpoints_list = mp;
}

// void vfs_init(void) {
//     /* Инициализация VFS и монтирование procfs */
//     extern fsdriver_t procfs_driver;
//     vfs_mount_core("/proc", &procfs_driver);
// }
void vfs_init(void) {
    mountpoints_list = NULL;
    for (int i = 0; i < 32; i++) {
        fd_table[i].type = FT_EMPTY;
        fd_table[i].offset = 0;
        fd_table[i].private_data = NULL;
    }

    fd_table[0].type = FT_DEVICE;
    fd_table[1].type = FT_DEVICE;
    fd_table[2].type = FT_DEVICE;
    fd_table[0].private_data = &console_node;
    fd_table[1].private_data = &console_node;
    fd_table[2].private_data = &console_node;

    // монтирование procfs
    vfs_mount_core("/proc", &procfs_driver);
    vfs_mount_core("/dev", &devtmpfs_driver);
}

int32_t vfs_open(const char *path) {
    if (!path) return -14;
    return vfs_open_flags(path, 0);
}

int32_t vfs_open_flags(const char *path, uint32_t flags) {
    int fd = -1;
    for (int i = 3; i < MAX_FD; i++) {
        if (fd_table[i].type == FT_EMPTY) { fd = i; break; }
    }
    if (fd == -1) return -24; // -EMFILE
    vfs_node_t* node = (vfs_node_t*)malloc(sizeof(vfs_node_t));
    if (!node) return -12; // -ENOMEM
    /* Склеиваем путь к файлу с точки монтирования...
     * Освобождаем используемую память после
     * strncmp гарантирует (около 52%) что все пройдет успешно
     */
    extern mountpoint_t *mountpoints_list;

    mountpoint_t *best_mount = NULL;
    size_t best_length = 0;
    for (mountpoint_t *mp = mountpoints_list; mp != NULL; mp = mp->next) {
        size_t len = strlen(mp->path);
        if (len > best_length && strncmp(path, mp->path, len) == 0 &&
            (len == 1 || path[len] == '/' || path[len] == '\0')) {
            best_mount = mp;
            best_length = len;
        }
    }

    if (best_mount && best_length > 1) {
        const char *subpath = path + best_length;
        if (*subpath == '/') subpath++;
        int32_t result = best_mount->driver->open(subpath, node);
        if (result == 0) {
            fd_table[fd].type = FT_VFS_FILE;
            fd_table[fd].offset = 0;
            fd_table[fd].private_data = node;
            return fd;
        }

        free(node);
        return result;
    }
    int file_idx = -1;
    for (int i = 0; i < MAX_FILES; i++) {
        if (file_table[i].used && strcmp(file_table[i].name, path) == 0) {
            file_idx = i;
            break;
        }
    }

    if (file_idx == -1) {
        if (!(flags & 0x40)) {
            free(node);
            return -2;
        }
        file_idx = fs_create(path);
        if (file_idx == -1) {
            free(node);
            return -28;
        } // -ENOSPC
    }

    strcpy(node->name, file_table[file_idx].name);
    node->flags = file_table[file_idx].type == FS_DIRECTORY ? VFS_DIRECTORY : VFS_FILE;
    node->size = file_table[file_idx].size;
    node->internal_id = file_idx;
    node->read = node->flags == VFS_FILE ? vfs_ramfs_read : NULL;
    node->write = node->flags == VFS_FILE ? vfs_ramfs_write : NULL;

    fd_table[fd].type = FT_VFS_FILE;
    fd_table[fd].offset = 0;
    fd_table[fd].private_data = (void*)node;

    return fd;
}

int32_t vfs_read(int fd, void *buf, uint32_t count) {
    if (fd < 0 || fd >= MAX_FD || fd_table[fd].type == FT_EMPTY) return -9;
    if (!buf && count) return -14;
    if (fd_table[fd].type == FT_DEVICE) {
        vfs_node_t *node = (vfs_node_t*)fd_table[fd].private_data;
        return node && node->read ? node->read(node, fd_table[fd].offset, count, (uint8_t*)buf) : -9;
    }
    if (fd_table[fd].type == FT_PIPE_READ) return vfs_pipe_read((pipe_t*)fd_table[fd].private_data, (uint8_t*)buf, count);
    if (fd_table[fd].type == FT_UNIX_SOCKET) return vfs_socket_recv(fd, buf, count);
    if (fd_table[fd].type == FT_INET_SOCKET) return vfs_socket_recv(fd, buf, count);
    if (fd_table[fd].type != FT_VFS_FILE) return -9;
    vfs_node_t *node = (vfs_node_t*)fd_table[fd].private_data;
    if (!node || !node->read) return -9;
    int32_t result = node->read(node, fd_table[fd].offset, count, (uint8_t*)buf);
    if (result > 0) fd_table[fd].offset += result;
    return result;
}

int32_t vfs_write(int fd, const void *buf, uint32_t count) {
    if (fd < 0 || fd >= MAX_FD || fd_table[fd].type == FT_EMPTY) return -9;
    if (!buf && count) return -14;
    if (fd_table[fd].type == FT_DEVICE) {
        vfs_node_t *node = (vfs_node_t*)fd_table[fd].private_data;
        return node && node->write ? node->write(node, fd_table[fd].offset, count, (const uint8_t*)buf) : -9;
    }
    if (fd_table[fd].type == FT_PIPE_WRITE) return vfs_pipe_write((pipe_t*)fd_table[fd].private_data, (const uint8_t*)buf, count);
    if (fd_table[fd].type == FT_UNIX_SOCKET) return vfs_socket_send(fd, buf, count);
    if (fd_table[fd].type == FT_INET_SOCKET) return vfs_socket_send(fd, buf, count);
    if (fd_table[fd].type != FT_VFS_FILE) return -9;
    vfs_node_t *node = (vfs_node_t*)fd_table[fd].private_data;
    if (!node || !node->write) return -9;
    int32_t result = node->write(node, fd_table[fd].offset, count, (const uint8_t*)buf);
    if (result > 0) fd_table[fd].offset += result;
    return result;
}

int32_t vfs_close(int fd) {
    if (fd < 0 || fd >= MAX_FD || fd_table[fd].type == FT_EMPTY) return -9;
    if (fd_table[fd].type == FT_PIPE_READ || fd_table[fd].type == FT_PIPE_WRITE) {
        pipe_t *pipe = (pipe_t*)fd_table[fd].private_data;
        if (fd_table[fd].type == FT_PIPE_READ && pipe->readers > 0) pipe->readers--;
        if (fd_table[fd].type == FT_PIPE_WRITE && pipe->writers > 0) pipe->writers--;
        if (pipe->readers == 0 && pipe->writers == 0) pipe->used = 0;
    } else if (fd_table[fd].type == FT_UNIX_SOCKET) {
        unix_socket_t *socket = (unix_socket_t*)fd_table[fd].private_data;
        if (socket->peer < UNIX_SOCKET_COUNT) unix_sockets[socket->peer].connected = 0;
        socket->used = 0;
    } else if (fd_table[fd].type == FT_INET_SOCKET) {
        inet_socket_t *socket = (inet_socket_t*)fd_table[fd].private_data;
        socket->used = 0;
    } else {
        if (fd_table[fd].private_data) free(fd_table[fd].private_data);
    }
    fd_table[fd].type = FT_EMPTY;
    fd_table[fd].offset = 0;
    fd_table[fd].private_data = NULL;
    return 0;
}

int32_t vfs_pipe(int32_t pipefd[2]) {
    pipe_t *pipe = NULL;
    for (int i = 0; i < PIPE_COUNT; i++) {
        if (!pipes[i].used) { pipe = &pipes[i]; break; }
    }
    if (!pipe) return -24;
    int32_t read_fd = -1;
    int32_t write_fd = -1;
    pipe->read_pos = 0;
    pipe->write_pos = 0;
    pipe->count = 0;
    pipe->readers = 1;
    pipe->writers = 1;
    pipe->used = 1;
    read_fd = vfs_allocate_fd(FT_PIPE_READ, pipe);
    if (read_fd < 0) { pipe->used = 0; return read_fd; }
    write_fd = vfs_allocate_fd(FT_PIPE_WRITE, pipe);
    if (write_fd < 0) { fd_table[read_fd].type = FT_EMPTY; pipe->used = 0; return write_fd; }
    pipefd[0] = read_fd;
    pipefd[1] = write_fd;
    return 0;
}

struct unix_sockaddr {
    uint16_t family;
    char path[UNIX_SOCKET_PATH];
};

static int32_t vfs_socket_address(const void *address, uint32_t address_length, char *path) {
    if (!address || address_length < 3) return -14;
    const struct unix_sockaddr *socket_address = (const struct unix_sockaddr*)address;
    if (socket_address->family != 1) return -97;
    uint32_t i = 0;
    while (i + 1 < UNIX_SOCKET_PATH && i + 2 < address_length && socket_address->path[i]) {
        path[i] = socket_address->path[i];
        i++;
    }
    path[i] = 0;
    return i ? 0 : -22;
}

int32_t vfs_socket(int domain, int type, int protocol) {
    if (domain == 2 && type == 2 && protocol == 0) {
        for (int i = 0; i < INET_SOCKET_COUNT; i++) {
            if (!inet_sockets[i].used) {
                inet_sockets[i].used = 1;
                inet_sockets[i].connected = 0;
                inet_sockets[i].address = 0;
                inet_sockets[i].port = 0;
                inet_sockets[i].count = 0;
                int32_t fd = vfs_allocate_fd(FT_INET_SOCKET, &inet_sockets[i]);
                if (fd < 0) inet_sockets[i].used = 0;
                return fd;
            }
        }
        return -24;
    }
    if (domain != 1 || type != 1 || protocol != 0) return -38;
    for (int i = 0; i < UNIX_SOCKET_COUNT; i++) {
        if (!unix_sockets[i].used) {
            unix_sockets[i].used = 1;
            unix_sockets[i].listening = 0;
            unix_sockets[i].connected = 0;
            unix_sockets[i].peer = UNIX_SOCKET_NONE;
            unix_sockets[i].pending = UNIX_SOCKET_NONE;
            int32_t fd = vfs_allocate_fd(FT_UNIX_SOCKET, &unix_sockets[i]);
            if (fd < 0) unix_sockets[i].used = 0;
            return fd;
        }
    }
    return -24;
}

int32_t vfs_socket_bind(int fd, const void *address, uint32_t address_length) {
    if (fd >= 0 && fd < MAX_FD && fd_table[fd].type == FT_INET_SOCKET) {
        inet_socket_t *socket = vfs_inet_socket_from_fd(fd);
        uint32_t ip;
        uint16_t port;
        int32_t result = vfs_inet_address(address, address_length, &ip, &port);
        if (result < 0) return result;
        for (int i = 0; i < INET_SOCKET_COUNT; i++) {
            if (&inet_sockets[i] != socket && inet_sockets[i].used && inet_sockets[i].port == port &&
                (inet_sockets[i].address == 0 || ip == 0 || inet_sockets[i].address == ip)) return -98;
        }
        socket->address = ip;
        socket->port = port;
        return 0;
    }
    if (fd < 0 || fd >= MAX_FD || fd_table[fd].type != FT_UNIX_SOCKET) return -9;
    unix_socket_t *socket = (unix_socket_t*)fd_table[fd].private_data;
    char path[UNIX_SOCKET_PATH];
    int32_t result = vfs_socket_address(address, address_length, path);
    if (result < 0) return result;
    for (int i = 0; i < UNIX_SOCKET_COUNT; i++) if (&unix_sockets[i] != socket && unix_sockets[i].used && strcmp(unix_sockets[i].path, path) == 0) return -98;
    strcpy(socket->path, path);
    return 0;
}

int32_t vfs_socket_listen(int fd, int backlog) {
    (void)backlog;
    if (fd < 0 || fd >= MAX_FD || fd_table[fd].type != FT_UNIX_SOCKET) return -9;
    unix_socket_t *socket = (unix_socket_t*)fd_table[fd].private_data;
    if (!socket->path[0]) return -22;
    socket->listening = 1;
    return 0;
}

int32_t vfs_socket_connect(int fd, const void *address, uint32_t address_length) {
    if (fd >= 0 && fd < MAX_FD && fd_table[fd].type == FT_INET_SOCKET) {
        inet_socket_t *socket = vfs_inet_socket_from_fd(fd);
        uint32_t ip;
        uint16_t port;
        int32_t result = vfs_inet_address(address, address_length, &ip, &port);
        if (result < 0) return result;
        if (socket->port == 0) socket->port = 49152 + (uint16_t)(fd - 3);
        socket->peer_address = ip;
        socket->peer_port = port;
        socket->connected = 1;
        return 0;
    }
    if (fd < 0 || fd >= MAX_FD || fd_table[fd].type != FT_UNIX_SOCKET) return -9;
    unix_socket_t *client = (unix_socket_t*)fd_table[fd].private_data;
    char path[UNIX_SOCKET_PATH];
    int32_t result = vfs_socket_address(address, address_length, path);
    if (result < 0) return result;
    for (int i = 0; i < UNIX_SOCKET_COUNT; i++) {
        unix_socket_t *server = &unix_sockets[i];
        if (server->used && server->listening && strcmp(server->path, path) == 0) {
            client->peer = i;
            client->connected = 1;
            server->pending = fd;
            return 0;
        }
    }
    return -2;
}

int32_t vfs_socket_accept(int fd) {
    if (fd < 0 || fd >= MAX_FD || fd_table[fd].type != FT_UNIX_SOCKET) return -9;
    unix_socket_t *server = (unix_socket_t*)fd_table[fd].private_data;
    if (!server->listening || server->pending == UNIX_SOCKET_NONE) return -11;
    int client_fd = server->pending;
    server->pending = UNIX_SOCKET_NONE;
    if (client_fd < 0 || client_fd >= MAX_FD || fd_table[client_fd].type != FT_UNIX_SOCKET) return -11;
    unix_socket_t *client = (unix_socket_t*)fd_table[client_fd].private_data;
    int client_index = (int)(client - unix_sockets);
    int accepted_index = -1;
    for (int i = 0; i < UNIX_SOCKET_COUNT; i++) if (!unix_sockets[i].used) { accepted_index = i; break; }
    if (accepted_index < 0) return -24;
    unix_socket_t *accepted = &unix_sockets[accepted_index];
    accepted->used = 1;
    accepted->connected = 1;
    accepted->peer = client_index;
    client->peer = accepted_index;
    return vfs_allocate_fd(FT_UNIX_SOCKET, accepted);
}

int32_t vfs_socket_send(int fd, const void *buffer, uint32_t length) {
    if (fd >= 0 && fd < MAX_FD && fd_table[fd].type == FT_INET_SOCKET) {
        inet_socket_t *socket = vfs_inet_socket_from_fd(fd);
        if (!socket->connected) return -107;
        return vfs_inet_send(socket, (const uint8_t*)buffer, length, socket->peer_address, socket->peer_port);
    }
    if (fd < 0 || fd >= MAX_FD || fd_table[fd].type != FT_UNIX_SOCKET) return -9;
    unix_socket_t *socket = (unix_socket_t*)fd_table[fd].private_data;
    if (socket->peer >= UNIX_SOCKET_COUNT || !unix_sockets[socket->peer].used) return -107;
    return vfs_socket_queue_write(&unix_sockets[socket->peer], (const uint8_t*)buffer, length);
}

int32_t vfs_socket_recv(int fd, void *buffer, uint32_t length) {
    if (fd >= 0 && fd < MAX_FD && fd_table[fd].type == FT_INET_SOCKET) return vfs_socket_recvfrom(fd, buffer, length, NULL, NULL);
    if (fd < 0 || fd >= MAX_FD || fd_table[fd].type != FT_UNIX_SOCKET) return -9;
    return vfs_socket_queue_read((unix_socket_t*)fd_table[fd].private_data, (uint8_t*)buffer, length);
}

int32_t vfs_socket_sendto(int fd, const void *buffer, uint32_t length, const void *address, uint32_t address_length) {
    if (fd < 0 || fd >= MAX_FD || fd_table[fd].type != FT_INET_SOCKET) return -9;
    inet_socket_t *socket = vfs_inet_socket_from_fd(fd);
    uint32_t ip;
    uint16_t port;
    int32_t result = vfs_inet_address(address, address_length, &ip, &port);
    if (result < 0) return result;
    if (socket->port == 0) socket->port = 49152 + (uint16_t)(fd - 3);
    return vfs_inet_send(socket, (const uint8_t*)buffer, length, ip, port);
}

int32_t vfs_socket_recvfrom(int fd, void *buffer, uint32_t length, void *address, uint32_t *address_length) {
    if (fd < 0 || fd >= MAX_FD || fd_table[fd].type != FT_INET_SOCKET) return -9;
    inet_socket_t *socket = vfs_inet_socket_from_fd(fd);
    if (socket->count == 0) return -11;
    inet_datagram_t *datagram = &socket->datagrams[socket->head];
    uint32_t copied = datagram->length < length ? datagram->length : length;
    for (uint32_t i = 0; i < copied; i++) ((uint8_t*)buffer)[i] = datagram->data[i];
    if (address && address_length && *address_length >= 16) {
        struct inet_sockaddr *source = (struct inet_sockaddr*)address;
        source->family = 2;
        source->port = vfs_network_port(datagram->source_port);
        source->address = datagram->source_address;
        *address_length = 16;
    }
    socket->head = (socket->head + 1) % INET_DATAGRAM_COUNT;
    socket->count--;
    return copied;
}

extern mountpoint_t *mountpoints_list;

int32_t vfs_mount(const char *target, fsdriver_t *driver) {
    if (!target || !driver) {
        return -1; // EINVAL
    }

    mountpoint_t *new_mp = (mountpoint_t *)malloc(sizeof(mountpoint_t));
    if (!new_mp) {
        return -1; // ENOMEM
    }

    strncpy(new_mp->path, target, sizeof(new_mp->path) - 1);
    new_mp->path[sizeof(new_mp->path) - 1] = '\0';

    new_mp->driver = driver;

    new_mp->next = mountpoints_list;
    mountpoints_list = new_mp;

    //debug_puts("Masix: Mount: Successfully mounted at: %s\n", new_mp->path);
    return 0;
}

