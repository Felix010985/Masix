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

#include <uapi.h>
#include <vfs.h>
#include <task.h>
#include <filesystem.h>
#include <syscall.h>
#include <kstat.h>
#include <pmm.h>
#include <vmm.h>

#include <drivers/pit.h>

#include <linux/dirent.h>
#include <linux/ioctl.h>
#include <linux/errno.h>
#include <linux/rwlock.h>
#include <sys/utsname.h>

#include <tty.h>

extern fsdriver_t procfs_driver;
extern fsdriver_t devtmpfs_driver;
extern mountpoint_t *mountpoints_list;

#define S_IFMT   0170000
#define S_IFREG  0100000
#define S_IFDIR  0040000
#define S_IFCHR  0020000
#define S_IFIFO  0010000
#define S_IFSOCK 0140000

/* Структура utsname для uname сисколла */
struct utsname system_utsname = {
    .sysname = "Masix",
    .nodename = "masix-box",
    .release = "3.0.2",
    .version = "#1 SMP Sat Oct 17 14:25:00 UTC 2026",
    .machine = "i686"
};

/* rwlock для предотвращения race condition */
rwlock_t uts_lock = RW_LOCK_UNLOCKED;

static void fill_kstat(vfs_node_t *node, int type, struct kstat *st) {
    memset(st, 0, sizeof(*st));
    switch (type) {
        case FT_DEVICE:    st->st_mode = S_IFCHR | 0666; break;
        case FT_PIPE_READ:
        case FT_PIPE_WRITE: st->st_mode = S_IFIFO | 0666; break;
        case FT_UNIX_SOCKET:
        case FT_INET_SOCKET: st->st_mode = S_IFSOCK | 0666; break;
        default:
            if (node && node->flags == VFS_DIRECTORY) st->st_mode = S_IFDIR | 0755;
            else st->st_mode = S_IFREG | 0644;
            break;
    }
    st->st_nlink = 1;
    st->st_uid = 0;
    st->st_gid = 0;
    st->st_blksize = 512;
    if (node) {
        st->st_size = node->size;
        st->st_ino = node->internal_id >= 0 ? (uint64_t)node->internal_id : 1;
    }
    st->st_blocks = (st->st_size + 511) / 512;
    st->st_dev = 0;
    st->st_rdev = 0;
}

int32_t sys_mount(const char *source, const char *target, const char *filesystemtype, unsigned long flags, const void *data) {
    (void)source; (void)flags; (void)data;
    if (!target || !filesystemtype) {
        return -1;
    }

    fsdriver_t *selected_driver = NULL;
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

    if (vfs_fd_table()[oldfd].type == FT_EMPTY) {
        return -9; // -EBADF
    }

    if (oldfd == newfd) {
        return newfd;
    }

    if (vfs_fd_table()[newfd].type != FT_EMPTY) {
        vfs_close(newfd);
    }

    vfs_fd_table()[newfd].type = vfs_fd_table()[oldfd].type;
    vfs_fd_table()[newfd].offset = vfs_fd_table()[oldfd].offset;
    vfs_fd_table()[newfd].private_data = vfs_fd_table()[oldfd].private_data;

    return newfd;
}

int32_t sys_read(int fd, char *buf, uint32_t count) {
    return vfs_read(fd, buf, count);
}

int32_t sys_write(int fd, const char *buf, uint32_t count) {
    return vfs_write(fd, buf, count);
}

int32_t sys_fstat(int fd, struct kstat *st) {
    if (!st) return -14; // -EFAULT
    if (fd < 0 || fd >= MAX_FD || vfs_fd_table()[fd].type == FT_EMPTY) return -9; // -EBADF
    fill_kstat((vfs_node_t*)vfs_fd_table()[fd].private_data, vfs_fd_table()[fd].type, st);
    return 0;
}

int32_t sys_fstat64(int fd, struct kstat *st) {
    return sys_fstat(fd, st);
}

int32_t sys_fstatat(int fd, const char *path, struct kstat *st, int flags) {
    if (!st) return -14;
    if (path && path[0] == '\0' && (flags & AT_EMPTY_PATH)) {
        return sys_fstat(fd, st);
    }
    if (!path) return -14;

    char full[MAX_PATH];
    if (path[0] == '/') {
        strncpy(full, path, MAX_PATH - 1);
    } else {
        if (!current_task) return -1;
        strncpy(full, current_task->cwd, MAX_PATH - 1);
        uint32_t len = strlen(full);
        if (len > 0 && full[len - 1] != '/') strncat(full, "/", MAX_PATH - len - 1);
        strncat(full, path, MAX_PATH - strlen(full) - 1);
    }
    full[MAX_PATH - 1] = '\0';

    int32_t real_fd = vfs_open(full);
    if (real_fd < 0) return real_fd;
    vfs_node_t *node = (vfs_node_t*)vfs_fd_table()[real_fd].private_data;
    fill_kstat(node, vfs_fd_table()[real_fd].type, st);
    vfs_close(real_fd);
    return 0;
}

/* readlink: отдает цель симлинка без завершающего NUL. Промежуточные
 * ссылки в пути VFS разворачивает сам (vfs_open), а вот последнюю
 * ссылку нужно открыть как есть - поэтому ищем запись напрямую. */
int32_t sys_readlink(const char *path, char *buf, uint32_t bufsiz) {
    if (!path || !buf) return -14; // -EFAULT
    if (bufsiz == 0) return -22;   // -EINVAL

    char full[MAX_PATH];
    if (path[0] == '/') {
        strncpy(full, path, MAX_PATH - 1);
    } else {
        if (!current_task) return -1;
        strncpy(full, current_task->cwd, MAX_PATH - 1);
        uint32_t len = strlen(full);
        if (len > 0 && full[len - 1] != '/') strncat(full, "/", MAX_PATH - len - 1);
        strncat(full, path, MAX_PATH - strlen(full) - 1);
    }
    full[MAX_PATH - 1] = '\0';

    /* /proc/self/fd/N (и /proc/<pid>/fd/N, /dev/fd/N): цель - путь к
     * открытому устройству. Так musl ttyname() и busybox login узнают имя
     * своего терминала; без этого root не проходит проверку /etc/securetty. */
    {
        const char *q = NULL;
        if (strncmp(full, "/proc/", 6) == 0) {
            q = full + 6;
            if (strncmp(q, "self/", 5) == 0) q += 5;
            else if (strncmp(q, "thread-self/", 12) == 0) q += 12;
            else { while (*q >= '0' && *q <= '9') q++; if (*q == '/') q++; }
        } else if (strncmp(full, "/dev/fd/", 8) == 0) {
            q = full + 8;
        }
        if (q && strncmp(q, "fd/", 3) == 0) {
            int fd = 0;
            const char *d = q + 3;
            while (*d >= '0' && *d <= '9') { fd = fd * 10 + (*d - '0'); d++; }
            if (*d == '\0' && fd >= 0 && fd < MAX_FD &&
                vfs_fd_table()[fd].type != FT_EMPTY) {
                vfs_node_t *n = (vfs_node_t*)vfs_fd_table()[fd].private_data;
                const char *dn = (n && n->name[0]) ? n->name : "console";
                char dev[MAX_PATH];
                strcpy(dev, "/dev/");
                strncat(dev, dn, sizeof(dev) - 6);
                uint32_t len = strlen(dev);
                if (len > bufsiz) len = bufsiz;
                memcpy(buf, dev, len);
                return (int32_t)len;
            }
            return -2; // -ENOENT
        }
    }

    for (int i = 0; i < MAX_FILES; i++) {
        if (!file_table[i].used || strcmp(file_table[i].name, full) != 0) continue;
        if (file_table[i].type != FS_SYMLINK) return -22; // -EINVAL
        const unsigned char *target = file_table[i].external_data;
        if (!target) return -22;
        uint32_t n = file_table[i].size;
        if (n > bufsiz) n = bufsiz;
        memcpy(buf, target, n);
        return (int32_t)n;
    }
    return -2; // -ENOENT
}

/* access/faccessat: модели прав у нас нет - проверяем лишь существование
 * пути. busybox и login используют это для проверки файлов вроде
 * /etc/nologin и ~/.hushlogin. */
int32_t sys_access(const char *path, int mode) {
    (void)mode;
    if (!path) return -14; // -EFAULT

    char full[MAX_PATH];
    if (path[0] == '/') {
        strncpy(full, path, MAX_PATH - 1);
    } else {
        if (!current_task) return -1;
        strncpy(full, current_task->cwd, MAX_PATH - 1);
        uint32_t len = strlen(full);
        if (len > 0 && full[len - 1] != '/') strncat(full, "/", MAX_PATH - len - 1);
        strncat(full, path, MAX_PATH - strlen(full) - 1);
    }
    full[MAX_PATH - 1] = '\0';

    int32_t fd = vfs_open(full);
    if (fd < 0) return fd;
    vfs_close(fd);
    return 0;
}

int32_t sys_ioctl(int fd, uint32_t request, void *argp) {
    if (fd < 0 || fd >= MAX_FD || vfs_fd_table()[fd].type == FT_EMPTY) return -9;
    switch (request) {
        case TIOCGWINSZ: {
            struct winsize *ws = (struct winsize *)argp;
            if (!ws) return -14;
            ws->ws_row = (unsigned short)tty_height();
            ws->ws_col = (unsigned short)tty_width();
            ws->ws_xpixel = 0;
            ws->ws_ypixel = 0;
            return 0;
        }
        case FIONREAD: {
            int *n = (int *)argp;
            if (!n) return -14;
            *n = 0;
            return 0;
        }
        case TCGETS:
        case TCSETS:
        case TCSETSW:
        case TCSETSF:
        case TIOCSWINSZ:
        case TIOCSPGRP:
        case TIOCSCTTY:
        case TIOCNOTTY:
        case FIOCLEX:
        case FIONCLEX:
            /* Терминал в сыром режиме не поддерживается, но не считаем ошибкой */
            return 0;
        case TIOCGPGRP: {
            /* tcgetpgrp(): оболочки (ash/bash) крутят
             *   while (tcgetpgrp() != getpgrp()) kill(-getpgrp(), SIGTTIN);
             * чтобы дождаться переднего плана. Группы процессов у нас
             * совпадают с PID, поэтому отдаем PID задачи - иначе цикл
             * никогда не завершится. */
            int *pgrp = (int *)argp;
            if (!pgrp) return -14;
            *pgrp = (int)current_task->pid;
            return 0;
        }
        case TIOCGSID: {
            /* tcgetsid(): отдаем PID единственной сессии */
            int *sid = (int *)argp;
            if (!sid) return -14;
            *sid = 1;
            return 0;
        }
        default:
            return -25; // -ENOTTY
    }
}

int32_t sys_getdents64(int fd, void *dirp, uint32_t count) {
    if (fd < 0 || fd >= MAX_FD || vfs_fd_table()[fd].type != FT_VFS_FILE) return -9;
    vfs_node_t *node = (vfs_node_t*)vfs_fd_table()[fd].private_data;
    if (!node || node->flags != VFS_DIRECTORY) return -20; // -ENOTDIR

    /* Собираем прямых потомков директории node->name, дедуплицируем по имени */
    static char used[64][256];
    int used_count = 0;
    int ordinal = 0;
    uint8_t *out = (uint8_t*)dirp;
    uint32_t written = 0;

    for (int i = 0; i < MAX_FILES; i++) {
        if (!file_table[i].used) continue;
        const char *name = file_table[i].name;
        const char *dir = node->name;
        uint32_t dir_len = strlen(dir);
        int is_root = (dir_len == 1 && dir[0] == '/');

        if (!is_root) {
            if (strncmp(name, dir, dir_len) != 0) continue;
            if (name[dir_len] != '/') continue;
        }
        const char *rest = is_root ? name + 1 : name + dir_len + 1;
        if (!*rest) continue;

        char comp[256];
        const char *slash = strchr(rest, '/');
        int is_dir = slash != NULL || file_table[i].type == FS_DIRECTORY;
        int comp_len = slash ? (slash - rest) : (int)strlen(rest);
        if (comp_len <= 0 || comp_len >= 256) continue;
        strncpy(comp, rest, comp_len);
        comp[comp_len] = '\0';

        int dup = 0;
        for (int u = 0; u < used_count; u++) {
            if (strcmp(used[u], comp) == 0) { dup = 1; break; }
        }
        if (dup || used_count >= 64) continue;
        strcpy(used[used_count++], comp);

        if (ordinal++ < (int)vfs_fd_table()[fd].offset) continue;

        uint32_t reclen = (19 + comp_len + 1 + 7) & ~7;
        if (written + reclen > count) return written > 0 ? (int32_t)written : -105; /* ENOBUFS */

        struct linux_dirent64 *d = (struct linux_dirent64 *)(out + written);
        d->d_ino = (uint64_t)i;
        d->d_off = ordinal;
        d->d_reclen = (unsigned short)reclen;
        d->d_type = is_dir ? DT_DIR : DT_REG;
        strcpy(d->d_name, comp);
        written += reclen;
    }

    /* Псевдодиректории монтирований в корне */
    if (node->name[0] == '/' && node->name[1] == '\0') {
        const char *pseudo[] = {"proc", "dev", "sys"};
        for (unsigned p = 0; p < sizeof(pseudo)/sizeof(pseudo[0]); p++) {
            int dup = 0;
            for (int u = 0; u < used_count; u++) if (strcmp(used[u], pseudo[p]) == 0) dup = 1;
            if (dup) continue;
            int reclen = (19 + (int)strlen(pseudo[p]) + 1 + 7) & ~7;
            if (ordinal++ < (int)vfs_fd_table()[fd].offset) continue;
            if (written + reclen > count) return written > 0 ? (int32_t)written : -105;
            struct linux_dirent64 *d = (struct linux_dirent64 *)(out + written);
            d->d_ino = 0x1000 + p;
            d->d_off = ordinal;
            d->d_reclen = reclen;
            d->d_type = DT_DIR;
            strcpy(d->d_name, pseudo[p]);
            written += reclen;
        }
    }

    vfs_fd_table()[fd].offset = ordinal;
    return (int32_t)written;
}


int32_t sys_chdir(const char *path) {
    if (!path) return -14; /* -EFAULT: Битый указатель на строку пути */

    if (!current_task) return -1;

    char new_path[MAX_PATH];
    memset(new_path, 0, MAX_PATH);

    /* Нормализация пути: проверяем относительный путь или абсолютный */
    if (path[0] == '/') {
        strncpy(new_path, path, MAX_PATH - 1);
    } else {
        strncpy(new_path, current_task->cwd, MAX_PATH - 1);

        uint32_t len = strlen(new_path);
        if (len > 0 && new_path[len - 1] != '/') {
            strncat(new_path, "/", MAX_PATH - len - 1);
        }

        strncat(new_path, path, MAX_PATH - strlen(new_path) - 1);
    }

    /* Проверяем существование пути через VFS */
    int32_t fd = vfs_open(new_path);
    if (fd < 0) {
        return -2; /* -ENOENT */
    }

    file_t* file_desc = &vfs_fd_table()[fd];
    vfs_node_t* node = (vfs_node_t*)file_desc->private_data;

    if (!node || node->flags != VFS_DIRECTORY) {
        vfs_close(fd);
        return -20; /* -ENOTDIR */
    }

    vfs_close(fd);

    strncpy(current_task->cwd, new_path, MAX_PATH - 1);

    return 0;
}

static task_t* task_find_child(void) {
    for (task_t* t = task_list_head; ; t = t->next) {
        if (t->parent == current_task) return t;
        if (t->next == task_list_head) return NULL;
    }
}

int32_t sys_wait4(int pid, int *wstatus, int options, void *rusage) {
    (void)rusage;
    if (!current_task) return -10;

    for (;;) {
        if (current_task->dead_child_pid != 0 &&
            (pid == -1 || pid == 0 || pid == current_task->dead_child_pid)) {
            if (wstatus) *wstatus = current_task->dead_child_status << 8;
            int ret = current_task->dead_child_pid;
            current_task->dead_child_pid = 0;
            current_task->dead_child_status = 0;
            return ret;
        }

        if (!task_find_child()) return -10; /* ECHILD */
        if (options & 1 /* WNOHANG */) return 0;

        /* Даем PIT-прерыванию переключить контекст на детей */
        __asm__ __volatile__("hlt");
    }
}

int32_t sys_nanosleep(const void *req, void *rem) {
    (void)rem;
    const struct { long tv_sec; long tv_nsec; } *r = req;
    if (!r) return -14;
    uint32_t ticks = (uint32_t)r->tv_sec * 100 + (uint32_t)(r->tv_nsec / 10000000);
    pit_sleep(ticks);
    return 0;
}

int32_t sys_clock_gettime32(int clk, void *ts_) {
    (void)clk;
    struct { long tv_sec; long tv_nsec; } *ts = ts_;
    if (!ts) return -14;
    uint32_t ticks = pit_get_ticks();
    ts->tv_sec = ticks / 100;
    ts->tv_nsec = (long)(ticks % 100) * 10000000L;
    return 0;
}

int32_t sys_clock_gettime64(int clk, void *ts_) {
    (void)clk;
    struct { long long tv_sec; long long tv_nsec; } *ts = ts_;
    if (!ts) return -14;
    uint32_t ticks = pit_get_ticks();
    ts->tv_sec = ticks / 100;
    ts->tv_nsec = (long long)(ticks % 100) * 10000000LL;
    return 0;
}

int32_t sys_getrlimit(int resource, void *rlim_) {
    (void)resource;
    uint32_t *rlim = rlim_;
    if (!rlim) return -14;
    rlim[0] = 0xFFFFFFFFU; /* rlim_cur */
    rlim[1] = 0xFFFFFFFFU; /* rlim_max */
    return 0;
}

int32_t sys_brk(uint32_t new_brk) {
    
    if (!current_task) return -1;
    if (new_brk == 0) {
        return current_task->brk;
    }
    if (new_brk > current_task->brk) {
        /* Мапим новые страницы под расширение кучи процесса */
        uint32_t start = (current_task->brk + 0xFFF) & ~0xFFF;
        uint32_t end = (new_brk + 0xFFF) & ~0xFFF;
        for (uint32_t va = start; va < end; va += PAGE_SIZE) {
            uint32_t phys = pmm_alloc_zeroed();
            if (!phys) return -12; /* ENOMEM */
            if (vmm_user_map(va, phys, PAGE_WRITE) != 0) {
                return -12;
            }
        }
        current_task->brk = new_brk;
    }
    return current_task->brk;
}

int32_t sys_mmap2(uint32_t addr, uint32_t length, uint32_t prot, uint32_t flags, int fd, uint32_t pgoffset) {
    (void)addr; (void)prot; (void)flags; (void)fd; (void)pgoffset;
    if (!current_task) return -1;
    uint32_t aligned_len = (length + 4095) & ~4095;
    uint32_t allocated_addr = current_task->brk;

    uint32_t start = (allocated_addr + 0xFFF) & ~0xFFF;
    uint32_t end = start + aligned_len;
    for (uint32_t va = start; va < end; va += PAGE_SIZE) {
        uint32_t phys = pmm_alloc_zeroed();
        if (!phys) return -12;
        if (vmm_user_map(va, phys, PAGE_WRITE) != 0) {
            return -12;
        }
    }

    current_task->brk = end;
    return start;
}
