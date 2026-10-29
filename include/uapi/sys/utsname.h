/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef _UAPI_SYS_UTSNAME_H
#define _UAPI_SYS_UTSNAME_H

/* Длина строк в структуре utsname */
#define _UTSNAME_LENGTH 65

struct utsname {
    char sysname[_UTSNAME_LENGTH];  /* Имя ядра (например, "Masix") */
    char nodename[_UTSNAME_LENGTH]; /* Имя узла в сети (hostname) */
    char release[_UTSNAME_LENGTH];  /* Текущий релиз ядра */
    char version[_UTSNAME_LENGTH];  /* Версия релиза и дата сборки */
    char machine[_UTSNAME_LENGTH];  /* Архитектура процессора (например, "i686") */
    #ifdef _GNU_SOURCE
    char domainname[_UTSNAME_LENGTH]; /* NIS или YP домен (в современных реалиях он пустой afaik) */
    #endif
};

/* Объявляем переменные как extern, чтобы они были видны другим модулям ядра */
extern struct utsname system_utsname;

#ifdef __KERNEL__
#include <linux/rwlock.h> // или ваш аналог для rwlock_t
extern rwlock_t uts_lock;
#endif

#endif
