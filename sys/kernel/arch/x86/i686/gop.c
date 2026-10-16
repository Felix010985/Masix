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

#include <boot.h>
#include <config.h>

/* Запрос линейного фреймбуфера у GOP (EFI Graphics Output Protocol).
 * Загрузчик в этом не участвует: ядро само находит таблицу EFI System Table,
 * спрашивает у протокола список режимов и включает нужный.
 *
 * Файл специфичен для i686: в таблицах EFI указатели 32 бита.
 * Для 64-битной сборки появится свой файл с теми же смещениями,
 * но 64-битными указателями. */

#define EFI_SUCCESS 0

/* Подпись таблицы EFI System Table: "IBI SYST". */
#define EFI_SYSTEM_TABLE_SIGNATURE 0x5453595320494249

struct efi_guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
};

/* EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID. */
static const struct efi_guid gop_guid = {
    0x9042a9de, 0x23dc, 0x4a38,
    { 0x96, 0xfb, 0x7a, 0xde, 0xd0, 0x80, 0x51, 0x6a }
};

struct efi_table_header {
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
};

/* IA-32: все указатели 32 бита. */
struct efi_system_table {
    struct efi_table_header hdr;        /* 0 */
    void* firmware_vendor;             /* 24 */
    uint32_t firmware_revision;        /* 28 */
    void* console_in_handle;           /* 32 */
    void* con_in;                      /* 36 */
    void* console_out_handle;          /* 40 */
    void* con_out;                     /* 44 */
    void* standard_error_handle;       /* 48 */
    void* std_err;                    /* 52 */
    void* runtime_services;            /* 56 */
    void* boot_services;              /* 60 */
    uint32_t number_of_table_entries;  /* 64 */
    void* configuration_table;        /* 68 */
};

struct efi_boot_services {
    struct efi_table_header hdr;       /* 0 */
    /* 24 .. 47: RaiseTPL, RestoreTPL, AllocatePages, FreePages,
     * GetMemoryMap, AllocatePool. */
    void* pad1[6];
    /* 48: FreePool, им освобождаются буферы, что выделяет QueryMode. */
    int (*free_pool)(void* buffer);
    /* 52 .. 171: остальные службы, ядру они не нужны. */
    void* pad2[30];
    /* 172: LocateProtocol. */
    int (*locate_protocol)(const struct efi_guid* protocol, void* registration,
                           void** interface);
};

struct efi_graphics_output_mode_information {
    uint32_t version;
    uint32_t horizontal_resolution;
    uint32_t vertical_resolution;
    uint32_t pixel_format;
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t reserved_mask;
    uint32_t pixels_per_scanline;
};

struct efi_graphics_output_protocol_mode {
    uint32_t max_mode;
    uint32_t mode;
    struct efi_graphics_output_mode_information* info;
    uint32_t size_of_info;
    uint64_t frame_buffer_base;
    uint64_t frame_buffer_size;
};

struct efi_graphics_output_protocol {
    int (*query_mode)(struct efi_graphics_output_protocol* self, uint32_t mode_number,
                      uint32_t* size_of_info,
                      struct efi_graphics_output_mode_information** info);
    int (*set_mode)(struct efi_graphics_output_protocol* self, uint32_t mode_number);
    int (*blt)(struct efi_graphics_output_protocol* self, void* blt_buffer,
               uint32_t mode, uint32_t src_x, uint32_t src_y,
               uint32_t dest_x, uint32_t dest_y,
               uint32_t width, uint32_t height, uint32_t delta);
    struct efi_graphics_output_protocol_mode* mode;
};

/* Форматы пикселя GOP. */
#define GOP_PIXEL_RED_GREEN_BLUE 0
#define GOP_PIXEL_BLUE_GREEN_RED 1
#define GOP_PIXEL_BIT_MASK       2
#define GOP_PIXEL_BLT_ONLY       3

/* Проверка смещений: они обязаны совпадать со спецификацией EFI,
 * иначе ядро прочитает не те поля. */
typedef char gop_assert_free_pool[(offsetof(struct efi_boot_services, free_pool) == 48) ? 1 : -1];
typedef char gop_assert_locate[(offsetof(struct efi_boot_services, locate_protocol) == 172) ? 1 : -1];

/* Перевести маску цвета в позицию и размер поля. */
static void gop_mask_fields(uint32_t mask, uint8_t* position, uint8_t* size) {
    uint32_t m = mask;
    uint8_t pos = 0;

    while ((m & 1) == 0 && pos < 32) {
        m >>= 1;
        pos++;
    }

    uint8_t count = 0;
    while ((m & 1) != 0) {
        m >>= 1;
        count++;
    }

    *position = pos;
    *size = count;
}

/* Сколько байт на пиксель в данном формате GOP. */
static uint32_t gop_bytes_per_pixel(struct efi_graphics_output_mode_information* info) {
    if (info->pixel_format == GOP_PIXEL_BIT_MASK) {
        uint32_t mask = info->red_mask | info->green_mask | info->blue_mask;
        uint32_t bits = 0;

        for (uint32_t i = 0; i < 32; i++) {
            if (mask & (1u << i)) bits++;
        }

        uint32_t bytes = (bits + 7) / 8;
        return bytes == 0 ? 4 : bytes;
    }

    return 4;
}

/* Заполнить описание режима по сведениям GOP. */
static void gop_fill_info(struct framebuffer_info* out,
                          struct efi_graphics_output_mode_information* info,
                          uint64_t base) {
    uint32_t red = info->red_mask;
    uint32_t green = info->green_mask;
    uint32_t blue = info->blue_mask;

    if (info->pixel_format == GOP_PIXEL_RED_GREEN_BLUE) {
        red = 0x00FF0000;
        green = 0x0000FF00;
        blue = 0x000000FF;
    } else if (info->pixel_format == GOP_PIXEL_BLUE_GREEN_RED) {
        red = 0x000000FF;
        green = 0x0000FF00;
        blue = 0x00FF0000;
    }

    uint32_t bytes = gop_bytes_per_pixel(info);

    memset(out, 0, sizeof(*out));

    out->address = (uintptr_t)base;
    out->width = info->horizontal_resolution;
    out->height = info->vertical_resolution;
    /* GOP отдает шаг в пикселях, драйверу нужен шаг в байтах. */
    out->pitch = info->pixels_per_scanline * bytes;
    out->bpp = (uint8_t)(bytes * 8);

    gop_mask_fields(red, &out->red_position, &out->red_mask_size);
    gop_mask_fields(green, &out->green_position, &out->green_mask_size);
    gop_mask_fields(blue, &out->blue_position, &out->blue_mask_size);

    out->valid = 1;
}

int gop_request(struct framebuffer_info* out, uint64_t system_table, int allow_mode_set) {
    memset(out, 0, sizeof(*out));

    if (system_table == 0 || system_table > 0xFFFFFFFFULL) {
        debug_puts("Masix: EFI system table is out of 32-bit range\n");
        return -1;
    }

    struct efi_system_table* st = (struct efi_system_table*)(uintptr_t)system_table;

    if (st->hdr.signature != EFI_SYSTEM_TABLE_SIGNATURE) {
        debug_puts("Masix: EFI system table signature mismatch\n");
        return -1;
    }

    struct efi_boot_services* bs = (struct efi_boot_services*)st->boot_services;
    if (bs == NULL) {
        debug_puts("Masix: EFI boot services table is missing\n");
        return -1;
    }

    struct efi_graphics_output_protocol* gop = NULL;
    if (bs->locate_protocol(&gop_guid, NULL, (void**)&gop) != EFI_SUCCESS || gop == NULL) {
        debug_puts("Masix: EFI graphics output protocol not found\n");
        return -1;
    }

    if (gop->mode == NULL || gop->mode->info == NULL) {
        debug_puts("Masix: EFI graphics output mode is missing\n");
        return -1;
    }

    if (gop->mode->frame_buffer_base == 0 || gop->mode->frame_buffer_base > 0xFFFFFFFFULL) {
        debug_puts("Masix: EFI framebuffer is not usable in 32-bit mode\n");
        return -1;
    }

    /* Режим меняется только пока живы boot services. Иначе берем тот режим,
     * который уже включила прошивка: после ExitBootServices звать GOP нельзя. */
    if (allow_mode_set) {
        uint32_t best = gop->mode->mode;
        uint64_t best_area = (uint64_t)gop->mode->info->horizontal_resolution *
                             (uint64_t)gop->mode->info->vertical_resolution;

        for (uint32_t i = 0; i < gop->mode->max_mode; i++) {
            if (i == gop->mode->mode) continue;

            uint32_t size = 0;
            struct efi_graphics_output_mode_information* info = NULL;

            if (gop->query_mode(gop, i, &size, &info) != EFI_SUCCESS || info == NULL) continue;

            /* Сведения копируем, буфер выделяет прошивка и его нужно вернуть. */
            struct efi_graphics_output_mode_information copy = *info;
            bs->free_pool(info);

            if (copy.pixel_format == GOP_PIXEL_BLT_ONLY) continue;

            uint64_t area = (uint64_t)copy.horizontal_resolution *
                            (uint64_t)copy.vertical_resolution;

            if (copy.horizontal_resolution >= 1024 && copy.vertical_resolution >= 768 &&
                area > best_area) {
                best = i;
                best_area = area;
            }
        }

        if (best != gop->mode->mode) {
            if (gop->set_mode(gop, best) == EFI_SUCCESS && gop->mode->info != NULL) {
                debug_puts("Masix: EFI graphics mode switched\n");
            }
        }
    }

    gop_fill_info(out, gop->mode->info, gop->mode->frame_buffer_base);
    return 0;
}
