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
#include <string.h>

#include <arch_boot.h>
#include <boot.h>
#include <config.h>

/* Выбор источника линейного фреймбуфера.
 *
 * Источник можно задать в командной строке ядра:
 *   fb=vbe    просить режим у VBE (BIOS)
 *   fb=gop    просить режим у GOP (EFI)
 *   fb=none   ничего не просить, консоль остается текстовой VGA
 *   fb=auto   поведение по умолчанию
 *
 * Почему по умолчанию не зовется VBE: в SeaBIOS внутри QEMU этот вызов
 * не возвращает управление, а незавершившийся вызов BIOS остановил бы
 * ядро целиком. Поэтому VBE включают запросом fb=vbe, а под UEFI режим
 * сразу спрашивают у GOP: это обычный вызов протокола, для него реальный
 * режим не нужен и зависать нечему. */

/* Режимы выбора источника. */
enum framebuffer_request_mode {
    FRAMEBUFFER_REQUEST_AUTO = 0,
    FRAMEBUFFER_REQUEST_VBE,
    FRAMEBUFFER_REQUEST_GOP,
    FRAMEBUFFER_REQUEST_NONE
};

/* Разобрать параметр fb=... в командной строке ядра. */
static enum framebuffer_request_mode framebuffer_request_parse(void) {
    const char* cmdline = boot_info.cmdline;
    if (cmdline == NULL || cmdline[0] == 0) return FRAMEBUFFER_REQUEST_AUTO;

    const char* p = cmdline;
    while (*p != 0) {
        while (*p == ' ') p++;
        if (*p == 0) break;

        if (p[0] == 'f' && p[1] == 'b' && p[2] == '=') {
            p += 3;

            if (p[0] == 'v' && p[1] == 'b' && p[2] == 'e') return FRAMEBUFFER_REQUEST_VBE;
            if (p[0] == 'g' && p[1] == 'o' && p[2] == 'p') return FRAMEBUFFER_REQUEST_GOP;
            if (p[0] == 'n' && p[1] == 'o' && p[2] == 'n' && p[3] == 'e') return FRAMEBUFFER_REQUEST_NONE;
            if (p[0] == 't' && p[1] == 'e' && p[2] == 'x' && p[3] == 't') return FRAMEBUFFER_REQUEST_NONE;
            if (p[0] == 'a' && p[1] == 'u' && p[2] == 't' && p[3] == 'o') return FRAMEBUFFER_REQUEST_AUTO;
        }

        while (*p != 0 && *p != ' ') p++;
    }

    return FRAMEBUFFER_REQUEST_AUTO;
}

int framebuffer_request_dispatch(struct framebuffer_info* out) {
    memset(out, 0, sizeof(*out));

    enum framebuffer_request_mode mode = framebuffer_request_parse();
    if (mode == FRAMEBUFFER_REQUEST_NONE) {
        debug_puts("Masix: framebuffer request disabled by fb=none\n");
        return -1;
    }

    /* GOP: только там, где есть таблица EFI System Table. */
    if (mode != FRAMEBUFFER_REQUEST_VBE && boot_info.firmware == BOOT_FIRMWARE_EFI &&
        boot_info.efi_system_table != 0) {
        if (gop_request(out, boot_info.efi_system_table, boot_info.efi_boot_services) == 0) {
            debug_puts("Masix: framebuffer requested from EFI graphics output protocol\n");
            return 0;
        }
    }

    /* VBE: только по явной просьбе fb=vbe, см. замечание в шапке файла. */
    if (mode == FRAMEBUFFER_REQUEST_VBE) {
        if (vbe_request(out) == 0) {
            debug_puts("Masix: framebuffer requested from VBE\n");
            return 0;
        }
    }

    debug_puts("Masix: no framebuffer source used, console stays on VGA text\n");
    return -1;
}
