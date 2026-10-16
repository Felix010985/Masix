/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel — bootstrap userland
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * Маленький непритязательный shell для проверки
 * взаимодействия с ядром. Без libc: ввод/вывод и
 * все системные вызовы - напрямую (см. syscall.h).
 *
 * Встроенные команды: help, clear, ver, fork, panic, exit.
 * Любое другое слово пробуется как /bin/<имя>.
 */
#include "syscall.h"
#include "lib.h"

#define LINE_MAX 256
#define ARGV_MAX 16

static const char PROMPT[] = "masix $ ";

static void putstr(const char* s) {
    sys_write(1, s, strlen(s));
}

/* Читает одну строку через редактор строки TTY. Эхо, backspace
 * и буферизация строки обрабатываются в ядре.
 * Возвращает длину строки или -1 при EOF. */
static int read_line(char* buf, int max) {
    int n = sys_read(0, buf, max);
    if (n <= 0) return -1;
    if (n > 0 && buf[n - 1] == '\n') {
        buf[n - 1] = '\0';
        n--;
    }
    return n;
}

/* Разбивает строку на аргументы по пробелам/табам. */
static int split(char* buf, char** argv) {
    int argc = 0;
    char* p = buf;

    while (*p && argc < ARGV_MAX - 1) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) *p++ = '\0';
    }
    argv[argc] = 0;
    return argc;
}

static void cmd_help(void) {
    putstr(
        "bootstrap shell commands:\n"
        "  help      show this text\n"
        "  clear     clear the screen (alias: c)\n"
        "  ver       kernel version from /proc/version\n"
        "  fork      fork/exit test\n"
        "  panic     halt the kernel (cli + hlt)\n"
        "  exit      leave the shell (init will restart it)\n"
        "  <name>    try to run /bin/<name>\n"
    );
}

static void cmd_ver(void) {
    int fd = sys_open("/proc/version", O_RDONLY, 0);
    if (fd < 0) {
        putstr("ver: /proc/version is unavailable\n");
        return;
    }

    char buf[128];
    long n = sys_read(fd, buf, sizeof(buf) - 1);
    sys_close(fd);

    if (n > 0) {
        buf[n] = '\0';
        putstr(buf);
        if (buf[n - 1] != '\n') putstr("\n");
    }
}

static void cmd_fork(void) {
    long pid = sys_fork();

    if (pid < 0) {
        putstr("fork: failed\n");
    } else if (pid == 0) {
        putstr("[CHILD] hello from the forked process, exiting now\n");
        sys_exit(0);
    } else {
        int status = 0;
        sys_wait4((int)pid, &status, 0, 0);
        putstr("[PARENT] child reaped\n");
    }
}

/* Пытаемся запустить /bin/<name> как отдельный процесс. */
static void run_command(char* name, char** argv) {
    char path[LINE_MAX];

    if (strlen(name) + 6 >= sizeof(path)) {
        putstr(name);
        putstr(": name too long\n");
        return;
    }
    strcpy(path, "/bin/");
    strcat(path, name);

    long pid = sys_fork();
    if (pid == 0) {
        char* envp[] = { 0 };
        sys_execve(path, argv, envp);
        putstr(name);
        putstr(": command not found\n");
        sys_exit(127);
    } else if (pid > 0) {
        int status = 0;
        sys_wait4((int)pid, &status, 0, 0);
    } else {
        putstr("fork: failed\n");
    }
}

int main(void);

__attribute__((noreturn)) void _start(void) {
    main();
    sys_exit_group(0);
    /* main() не должна возвращаться, но на всякий случай */
    for (;;) __asm__ __volatile__("hlt");
}

int main(void) {
    char line[LINE_MAX];
    char* argv[ARGV_MAX];

    for (;;) {
        putstr(PROMPT);

        if (read_line(line, sizeof(line)) < 0) {
            /* EOF (закрытый tty) - выходим, init перезапустит shell */
            putstr("\n");
            break;
        }

        int argc = split(line, argv);
        if (argc == 0) continue;

        if (strcmp(argv[0], "help") == 0) {
            cmd_help();
        } else if (strcmp(argv[0], "clear") == 0 || strcmp(argv[0], "c") == 0) {
            putstr("\033[2J\033[H");
        } else if (strcmp(argv[0], "ver") == 0) {
            cmd_ver();
        } else if (strcmp(argv[0], "fork") == 0) {
            cmd_fork();
        } else if (strcmp(argv[0], "panic") == 0) {
            putstr("panic: halting the kernel\n");
            __asm__ __volatile__("cli");
            for (;;) __asm__ __volatile__("hlt");
        } else if (strcmp(argv[0], "exit") == 0 || strcmp(argv[0], "quit") == 0) {
            break;
        } else {
            run_command(argv[0], argv);
        }
    }

    return 0;
}
