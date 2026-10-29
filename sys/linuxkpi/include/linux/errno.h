/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix LinuxKPI
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
#ifndef _LINUX_ERRNO_H
#define _LINUX_ERRNO_H

/* Подтягиваем базовые коды ошибок из публичного UAPI */
#include <uapi/sys/errno.h>

/*
 * Специфичные внутренние коды ошибок ядра Linux.
 * Они никогда не должны улетать в userland, ядро обрабатывает их внутри.
 */
#define ERESTARTSYS     512 /* Системный вызов должен быть перезапущен */
#define ERESTARTNOINTR  513 /* Перезапуск без проверки на прерывание сигналом */
#define ERESTARTNOHAND  514 /* Перезапуск, если нет обработчика сигнала */
#define ERESTART_RESTARTBLOCK 516 /* Перезапуск с помощью block-функции */

#define EPROBE_DEFER    517 /* Драйвер просит отложить инициализацию устройства */
#define EOPENSTALE      518 /* Устаревший дескриптор файла */

/* Вспомогательный макрос для проверки внутренних ошибок */
static inline int is_restart_errno(int err) {
    return (err >= ERESTARTSYS && err <= ERESTART_RESTARTBLOCK);
}

#endif
