/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Toolkit
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

const char* APPS[] = {"clang", "ld.lld", "grub-mkrescue", "qemu-system-x86_64", "cpio", "git"};
#define COUNT 6

const char* MANAGERS[] = {
    "/usr/bin/xbps-install",
    "/sbin/apk",
    "/usr/sbin/slackpkg",
    "/usr/sbin/pkg",
    "/usr/bin/pacman",
    "/usr/bin/apt-get"
};
/* КОД НИЖЕ:
 * Никакой гарантии что пакеты называются именно так, если что то уже не соответствует названиям здесь - извините.
 * У меня нет возможности проверить абсолютно все пункты здесь ибо мой хост это исключительно Void Linux и MazukiOS.
 * Использовать на свой страх и риск
 */
char* VOID_ARGS[]    = {"/usr/bin/sudo", "xbps-install", "-Sy", "clang", "lld", "llvm", "base-devel", "cpio", "grub", "qemu", "xorriso", "git", NULL};
char* ALPINE_ARGS[]  = {"/usr/bin/sudo", "apk", "add", "clang", "lld", "llvm", "build-base", "cpio", "grub", "xorriso", "qemu-system-x86_64", "git", NULL};
char* SLACK_ARGS[]   = {"/usr/bin/sudo", "slackpkg", "install", "clang", "lld", "llvm", "cpio", "grub", "qemu", "git", NULL};
char* FREEBSD_ARGS[] = {"/usr/bin/sudo", "pkg", "install", "-y", "llvm", "lld", "cpio", "grub2-mkrescue", "xorriso", "qemu-devel", "git", NULL};
char* ARCH_ARGS[]    = {"/usr/bin/sudo", "pacman", "-S", "--needed", "--noconfirm", "base-devel", "clang", "lld", "llvm", "cpio", "grub", "xorriso", "qemu-desktop", "git", NULL};
char* DEBIAN_ARGS[]  = {"/usr/bin/sudo", "apt-get", "install", "-y", "build-essential", "clang", "lld", "llvm", "cpio", "grub-pc-bin", "xorriso", "qemu-system-x86", "git", NULL};

char** ARGS_MAP[] = {VOID_ARGS, ALPINE_ARGS, SLACK_ARGS, FREEBSD_ARGS, ARCH_ARGS, DEBIAN_ARGS};

/* ===== Конфигурация сборки: цель, бэкенд отладки, уровень отладки ===== */

/* Поддерживаемые цели. Пока в дереве есть только 32-битная x86 (i686):
 * общий для amd64/i386 код лежит в sys/kernel/arch/x86/, а i686-специфичный
 * (32-битный бутстрап, GDT/IDT/TSS, 32-битные таблицы страниц) -
 * в sys/kernel/arch/x86/i686/. Новая цель добавляется сюда, в
 * sys/kernel/arch/<платформа>/ и со своим скриптом линкера
 * (linker_script), т.к. раскладка памяти у каждой цели своя. */
struct target_def {
    const char* name;          /* Имя для пользователя */
    const char* clang_target;  /* Значение -target для clang */
    const char* ld_emulation;  /* Значение -m для ld.lld */
    const char* arch_cflags;   /* -I пути к include нужной платформы */
    const char* linker_script; /* Скрипт линкера для этой цели */
};

static const struct target_def TARGETS[] = {
    {"i686", "i686-unknown-elf", "elf_i386",
     "-Isys/kernel/arch/x86/include -Isys/kernel/arch/x86/i686/include",
     "sys/kernel/arch/x86/i686/linker.ld"},
};
#define TARGET_COUNT (sizeof(TARGETS) / sizeof(TARGETS[0]))

struct option_def {
    const char* name;
    const char* define; /* Макрос для build_config.h */
};

/* Бэкенды отладочного вывода (макросы должны совпадать с
 * sys/kernel/include/config.h) */
static const struct option_def BACKENDS[] = {
    {"serial", "DEBUG_OUTPUT_SERIAL"},
    {"printk", "DEBUG_OUTPUT_PRINTK"},
    {"stdio",  "DEBUG_OUTPUT_STDIO"},
};
#define BACKEND_COUNT (sizeof(BACKENDS) / sizeof(BACKENDS[0]))

/* Уровни отладки.
 * Уровень ниже INFO полностью выключает отладочный вывод. */
struct level_def {
    const char* name;
    const char* macro; /* Имя макроса уровня из config.h (для комментария) */
    int value;         /* Числовое значение DEBUG_LEVEL */
};

static const struct level_def LEVELS[] = {
    {"none",  "DEBUG_LEVEL_NONE",  0},
    {"error", "DEBUG_LEVEL_ERROR", 1},
    {"info",  "DEBUG_LEVEL_INFO",  2},
    {"debug", "DEBUG_LEVEL_DEBUG", 3},
};
#define LEVEL_COUNT (sizeof(LEVELS) / sizeof(LEVELS[0]))

/* Текущий выбор. Значения по умолчанию совпадают с fallback'ами config.h */
static const struct target_def* cfg_target  = &TARGETS[0];
static const struct option_def* cfg_backend = &BACKENDS[0];
static const struct level_def* cfg_level   = &LEVELS[2]; /* info */

static const char* CONFIG_MK       = "config.mk";
static const char* BUILD_CONFIG_H  = "sys/kernel/include/build_config.h";

/* Есть ли токен в файле (для чтения текущей конфигурации) */
static int file_has_token(const char* path, const char* token) {
    FILE* f = fopen(path, "r");
    if (!f) return 0;

    char line[512];
    int found = 0;
    while (fgets(line, sizeof(line), f)) {
        if (strstr(line, token) != NULL) {
            found = 1;
            break;
        }
    }
    fclose(f);
    return found;
}

/* Загружает текущий выбор из сгенерированных файлов, если они есть */
static void load_current_config(void) {
    for (size_t i = 0; i < TARGET_COUNT; i++) {
        char needle[128];
        snprintf(needle, sizeof(needle), "-target %s", TARGETS[i].clang_target);
        if (file_has_token(CONFIG_MK, needle)) {
            cfg_target = &TARGETS[i];
            break;
        }
    }
    for (size_t i = 0; i < BACKEND_COUNT; i++) {
        if (file_has_token(BUILD_CONFIG_H, BACKENDS[i].define)) {
            cfg_backend = &BACKENDS[i];
            break;
        }
    }
    for (size_t i = 0; i < LEVEL_COUNT; i++) {
        char needle[64];
        snprintf(needle, sizeof(needle), "#define DEBUG_LEVEL %d", LEVELS[i].value);
        if (file_has_token(BUILD_CONFIG_H, needle)) {
            cfg_level = &LEVELS[i];
            break;
        }
    }
}

static int write_build_config(void) {
    FILE* f = fopen(BUILD_CONFIG_H, "w");
    if (!f) return -1;

    fprintf(f,
        "/* SPDX-License-Identifier: GPL-2.0-only */\n"
        "/*\n"
        " * Masix Kernel\n"
        " * Copyright (C) 2026, FelixProfi. All rights reserved.\n"
        " *\n"
        " * This program is free software; you can redistribute it and/or modify\n"
        " * it under the terms of the GNU General Public License as published by\n"
        " * the Free Software Foundation; version 2 of the License.\n"
        " */\n"
        "\n"
        "/* СГЕНЕРИРОВАНО tools/configure - НЕ РЕДАКТИРОВАТЬ ВРУЧНУЮ.\n"
        " * Перегенерировать: ./conf --target <цель> --backend <бэкенд>\n"
        " *                   --debug-level <уровень>                          */\n"
        "\n"
        "#ifndef KERNEL_BUILD_CONFIG_H\n"
        "#define KERNEL_BUILD_CONFIG_H\n"
        "\n"
        "#define %s\n"
        "#define DEBUG_LEVEL %d /* %s */\n"
        "\n"
        "#endif /* KERNEL_BUILD_CONFIG_H */\n",
        cfg_backend->define, cfg_level->value, cfg_level->macro);

    int rc = ferror(f);
    fclose(f);
    return rc ? -1 : 0;
}

static int write_config_mk(void) {
    FILE* f = fopen(CONFIG_MK, "w");
    if (!f) return -1;

    fprintf(f,
        "# Сгенерировано tools/configure - не редактировать вручную.\n"
        "# Перегенерировать: ./conf --target <цель> --backend <бэкенд>\n"
        "#                   --debug-level <уровень>\n"
        "\n"
        "TARGET_FLAGS := -target %s\n"
        "LINKER_SCRIPT := %s\n"
        "LDFLAGS := -m %s -T $(LINKER_SCRIPT) -n\n"
        "ARCH_CFLAGS := %s\n",
        cfg_target->clang_target, cfg_target->linker_script,
        cfg_target->ld_emulation, cfg_target->arch_cflags);

    int rc = ferror(f);
    fclose(f);
    return rc ? -1 : 0;
}

static int apply_config(void) {
    if (write_build_config()) {
        printf("[ FAIL ] Ошибка записи %s\n", BUILD_CONFIG_H);
        return -1;
    }
    if (write_config_mk()) {
        printf("[ FAIL ] Ошибка записи %s\n", CONFIG_MK);
        return -1;
    }
    return 0;
}

static void print_summary(void) {
    printf("  Целевая платформа : %s (clang -target %s, ld -m %s)\n",
           cfg_target->name, cfg_target->clang_target, cfg_target->ld_emulation);
    printf("  Бэкенд отладки    : %s\n", cfg_backend->name);
    printf("  Уровень отладки   : %s\n", cfg_level->name);
}

static int pick_target(void) {
    printf("Доступные цели:\n");
    for (size_t i = 0; i < TARGET_COUNT; i++) {
        printf(" %zu) %s\n", i + 1, TARGETS[i].name);
    }
    printf("Выбор [1-%zu]: ", TARGET_COUNT);

    char buf[64];
    if (!fgets(buf, sizeof(buf), stdin)) return -1;
    int n = atoi(buf);
    if (n < 1 || (size_t)n > TARGET_COUNT) {
        printf("Неверный выбор\n");
        return -1;
    }
    cfg_target = &TARGETS[n - 1];
    return 0;
}

static int pick_option(const char* title, const struct option_def* opts,
                       size_t count, const struct option_def** out) {
    printf("%s:\n", title);
    for (size_t i = 0; i < count; i++) {
        printf(" %zu) %s\n", i + 1, opts[i].name);
    }
    printf("Выбор [1-%zu]: ", count);

    char buf[64];
    if (!fgets(buf, sizeof(buf), stdin)) return -1;
    int n = atoi(buf);
    if (n < 1 || (size_t)n > count) {
        printf("Неверный выбор\n");
        return -1;
    }
    *out = &opts[n - 1];
    return 0;
}

static int pick_level(void) {
    printf("Уровни отладки:\n");
    for (size_t i = 0; i < LEVEL_COUNT; i++) {
        printf(" %zu) %s\n", i + 1, LEVELS[i].name);
    }
    printf("Выбор [1-%zu]: ", LEVEL_COUNT);

    char buf[64];
    if (!fgets(buf, sizeof(buf), stdin)) return -1;
    int n = atoi(buf);
    if (n < 1 || (size_t)n > LEVEL_COUNT) {
        printf("Неверный выбор\n");
        return -1;
    }
    cfg_level = &LEVELS[n - 1];
    return 0;
}

static void interactive_menu(void) {
    int dirty = 0;

    for (;;) {
        printf("=== Настройка MazukiOS ===\n");
        printf(" 1) Целевая платформа : %s\n", cfg_target->name);
        printf(" 2) Бэкенд отладки    : %s\n", cfg_backend->name);
        printf(" 3) Уровень отладки   : %s\n", cfg_level->name);
        printf(" 4) Записать конфигурацию\n");
        printf(" 0) Выход%s\n", dirty ? " (без сохранения)" : "");
        printf("Выбор: ");

        char buf[64];
        if (!fgets(buf, sizeof(buf), stdin)) break;

        int choice = atoi(buf);
        switch (choice) {
        case 1:
            if (pick_target() == 0) dirty = 1;
            break;
        case 2:
            if (pick_option("Бэкенды отладочного вывода", BACKENDS,
                            BACKEND_COUNT, &cfg_backend) == 0) dirty = 1;
            break;
        case 3:
            if (pick_level() == 0) dirty = 1;
            break;
        case 4:
            if (apply_config() == 0) {
                printf("Конфигурация записана:\n");
                print_summary();
                dirty = 0;
            }
            break;
        case 0:
            return;
        default:
            printf("Неверный пункт меню\n");
            break;
        }
    }
}

static const char* USAGE =
    "Использование: conf [опции]\n"
    "  --target <цель>        Целевая платформа (сейчас доступно: i686)\n"
    "  --backend <бэкенд>     Бэкенд отладки: serial, printk, stdio\n"
    "  --debug-level <уровень> Уровень отладки: none, error, info, debug\n"
    "  --show                 Показать текущую конфигурацию\n"
    "  --help                 Эта справка\n"
    "\n"
    "Без аргументов открывается интерактивное меню. Приятного пользования Masix!\n";

static int find_option(const struct option_def* opts, size_t count, const char* name) {
    for (size_t i = 0; i < count; i++) {
        if (strcmp(opts[i].name, name) == 0) return (int)i;
    }
    return -1;
}

static int find_level(const char* name) {
    for (size_t i = 0; i < LEVEL_COUNT; i++) {
        if (strcmp(LEVELS[i].name, name) == 0) return (int)i;
    }
    return -1;
}

int check_bin_in_path(const char* app) {
    char* path_env = getenv("PATH");
    if (!path_env) return 0;

    char* path_copy = malloc(strlen(path_env) + 1);
    if (!path_copy) return 0;
    strcpy(path_copy, path_env);

    char full_path[1024];
    int found = 0;

    char* token = strtok(path_copy, ":");
    while (token != NULL) {
        snprintf(full_path, sizeof(full_path), "%s/%s", token, app);
        if (access(full_path, X_OK) == 0) {
            found = 1;
            break;
        }
        token = strtok(NULL, ":");
    }

    free(path_copy);
    return found;
}

int run_command(char** args) {
    pid_t pid = fork();
    if (pid == 0) {
        execv(args[0], args);
        exit(1);
    } else if (pid > 0) {
        int status;
        waitpid(pid, &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            return 0;
        }
        return 1;
    }
    return 1;
}

int main(int argc, char** argv) {
    const char* CLR_RESET   = "\033[0m";
    const char* CLR_GREEN   = "\033[1;32m";
    const char* CLR_CYAN    = "\033[1;36m";
    const char* CLR_RED     = "\033[1;31m";

        /* Настройку можно делать только из корня репозитория */
    if (access("Makefile", F_OK) != 0 || access("sys/kernel/include/config.h", F_OK) != 0) {
        printf("[%s FAIL %s] Запускайте configure из корня репозитория MazukiOS\n", CLR_RED, CLR_RESET);
        return 1;
    }

    load_current_config();

    /* Разбор аргументов: неинтерактивный режим */
    int cfg_args = 0;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--target") == 0 && i + 1 < argc) {
            int idx = -1;
            for (size_t j = 0; j < TARGET_COUNT; j++) {
                if (strcmp(TARGETS[j].name, argv[i + 1]) == 0) { idx = (int)j; break; }
            }
            if (idx < 0) {
                printf("[%s FAIL %s] Неизвестная цель: %s (доступно: ", CLR_RED, CLR_RESET, argv[i + 1]);
                for (size_t j = 0; j < TARGET_COUNT; j++) printf("%s%s", j ? ", " : "", TARGETS[j].name);
                printf(")\n");
                return 1;
            }
            cfg_target = &TARGETS[idx];
            cfg_args = 1;
            i++;
        } else if (strcmp(argv[i], "--backend") == 0 && i + 1 < argc) {
            int idx = find_option(BACKENDS, BACKEND_COUNT, argv[i + 1]);
            if (idx < 0) {
                printf("[%s FAIL %s] Неизвестный бэкенд: %s (доступно: serial, printk, stdio)\n",
                       CLR_RED, CLR_RESET, argv[i + 1]);
                return 1;
            }
            cfg_backend = &BACKENDS[idx];
            cfg_args = 1;
            i++;
        } else if (strcmp(argv[i], "--debug-level") == 0 && i + 1 < argc) {
            int idx = find_level(argv[i + 1]);
            if (idx < 0) {
                printf("[%s FAIL %s] Неизвестный уровень отладки: %s (доступно: none, error, info, debug)\n",
                       CLR_RED, CLR_RESET, argv[i + 1]);
                return 1;
            }
            cfg_level = &LEVELS[idx];
            cfg_args = 1;
            i++;
        } else if (strcmp(argv[i], "--show") == 0) {
            printf("[%s INFO %s] Текущая конфигурация:\n", CLR_CYAN, CLR_RESET);
            print_summary();
            return 0;
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("%s", USAGE);
            return 0;
        } else {
            printf("[%s FAIL %s] Неизвестный аргумент: %s\n", CLR_RED, CLR_RESET, argv[i]);
            printf("%s", USAGE);
            return 1;
        }
    }

    if (cfg_args) {
        printf("[%s INFO %s] Запись конфигурации...\n", CLR_CYAN, CLR_RESET);
        if (apply_config()) {
            printf("[%s FAIL %s] Ошибка записи конфигурации\n", CLR_RED, CLR_RESET);
            return 1;
        }
        printf("[%s  OK  %s] Конфигурация записана:\n", CLR_GREEN, CLR_RESET);
        print_summary();
    } else if (isatty(STDIN_FILENO)) {
        interactive_menu();
    } else {
        printf("[%s INFO %s] Текущая конфигурация:\n", CLR_CYAN, CLR_RESET);
        print_summary();
        printf("[%s INFO %s] Запустите conf в терминале для меню, либо с аргументами\n", CLR_CYAN, CLR_RESET);
        printf("           --target / --backend / --debug-level (см. --help)\n");
    }

    printf("[%s INFO %s] Проверка env для MazukiOS\n", CLR_CYAN, CLR_RESET);

    int missing = 0;
    for (size_t i = 0; i < COUNT; i++) {
        if (check_bin_in_path(APPS[i])) {
            printf("[%s  OK  %s] Проверка утилиты: %s\n", CLR_GREEN, CLR_RESET, APPS[i]);
        } else {
            printf("[%s FAIL %s] Проверка утилиты: %s не найдена\n", CLR_RED, CLR_RESET, APPS[i]);
            missing++;
        }
    }

    if (missing > 0) {
        printf("\n[%s INFO %s] Обнаружен неполный тулчейн, определение пакетного менеджера...\n", CLR_CYAN, CLR_RESET);

        char** selected_args = NULL;
        int is_debian = 0;

        for (size_t i = 0; i < 6; i++) {
            if (access(MANAGERS[i], X_OK) == 0) {
                selected_args = ARGS_MAP[i];
                if (strcmp(MANAGERS[i], "/usr/bin/apt-get") == 0) {
                    is_debian = 1;
                }
                break;
            }
        }

        if (!selected_args) {
            printf("[%s FAIL %s] Ошибка env: пакетный менеджер хоста не определен\n", CLR_RED, CLR_RESET);
            return 1;
        }

        if (is_debian) {
            char* update_args[] = {"/usr/bin/sudo", "apt-get", "update", NULL};
            run_command(update_args);
        }

        printf("[%s INFO %s] Запуск автоматической установки недостающих пакетов...\n", CLR_CYAN, CLR_RESET);
        if (!run_command(selected_args)) {
            printf("[%s FAIL %s] Environment error: сбой при установке зависимостей\n", CLR_RED, CLR_RESET);
            return 1;
        }
        printf("[%s  OK  %s] Все системные пакеты успешно установлены\n", CLR_GREEN, CLR_RESET);
    }

    printf("[%s  OK  %s] Конфигурация успешно завершена! Env готов к работе\n", CLR_GREEN, CLR_RESET);
    return 0;
}
