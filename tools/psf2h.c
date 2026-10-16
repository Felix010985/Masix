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
#include <stdint.h>
/* Упрощение работы с цветом
 * TODO: создать отдельный файл с всем что нужно для папки tools/
 */
const char* CLR_RESET   = "\033[0m";
const char* CLR_GREEN   = "\033[1;32m";
const char* CLR_CYAN    = "\033[1;36m";
const char* CLR_RED     = "\033[1;31m";

struct psf1_header {
    uint8_t magic[2];     // 0x36, 0x04
    uint8_t mode;         // Режим шрифта (флаги, определяющие количество символов)
    uint8_t charsize;     // Высота каждого символа в пикселях
} __attribute__((packed));

struct psf2_header {
    uint8_t magic[4];  // 0x72, 0xb5, 0x4a, 0x86
    uint32_t version;
    uint32_t headersize;
    uint32_t flags;
    uint32_t length;
    uint32_t glyphsize;
    uint32_t height;
    uint32_t width;
}; __attribute__((packed));

int main(int argc, char* argv[]) {
    if (argc < 3) {
        printf("[%s FAIL %s] Утилита вызывается так: psf2h <input.psf> <output.h>!\n", CLR_RED, CLR_RESET);
        return 1;
    }

    FILE* in = fopen(argv[1], "rb");
    if (!in) return 1;

    struct psf2_header h;
    if (fread(&h, sizeof(struct psf2_header), 1, in) != 1) {
        fclose(in);
        return 1;
    }

    uint32_t width, height, glyph_size, length, header_size;

    if (h.magic[0] != 0x72 || h.magic[1] != 0xb5 || h.magic[2] != 0x4a || h.magic[3] != 0x86) {
        /* Если магия не совпадает с магией формата PSF, файл поврежден или не является PSF,
         * в printf при ошибке передаем argv[1] чтобы вывести конкретное имя файла, который не поддерживается
         */

        /* Проверяем, может это формат PSF1? Первые два байта должны быть 0x36 и 0x04 */
        if (h.magic[0] == 0x36 && h.magic[1] == 0x04) {
            width       = 8;
            height      = h.magic[3]; // Байт charsize в заголовке PSF1
            glyph_size  = h.magic[3]; // Для 8-битной ширины размер равен высоте
            length      = (h.magic[2] & 0x01) ? 512 : 256; // Байт mode определяет длину
            header_size = 4;
        } else {
            printf("[%s FAIL %s] Файл %s не является PSF!\n", CLR_RED, CLR_RESET, argv[1]);
            fclose(in);
            return 1;
        }
    } else {
        /* Если это родной PSF2, берем параметры из структуры */
        width       = h.width;
        height      = h.height;
        glyph_size  = h.glyphsize;
        length      = h.length;
        header_size = h.headersize;
    }

    FILE* out = fopen(argv[2], "w");
    if (!out) { fclose(in); return 1; }
    /* Необходимый код (boilerplate) для Masix, чтобы он понял что это шрифт
     */
    fprintf(out, "#pragma once\n\n");
    fprintf(out, "#include <stdint.h>\n\n");
    fprintf(out, "#define FONT_WIDTH      %u\n", width);
    fprintf(out, "#define FONT_HEIGHT     %u\n", height);
    fprintf(out, "#define FONT_GLYPH_SIZE %u\n", glyph_size);
    fprintf(out, "#define FONT_LENGTH     %u\n\n", length);
    fprintf(out, "static const uint8_t font_data[] = {\n    ");

    fseek(in, header_size, SEEK_SET);
    uint32_t total_bytes = length * glyph_size;
    for (uint32_t i = 0; i < total_bytes; i++) {
        int c = fgetc(in);
        if (c == EOF) break;
        fprintf(out, "0x%02X, ", c);
        if ((i + 1) % 12 == 0) fprintf(out, "\n    ");
    }
    printf("[%s INFO %s] Запись в конечный файл.\n", CLR_CYAN, CLR_RESET);
    fprintf(out, "\n};\n");

    fclose(in);
    fclose(out);
    printf("[%s INFO %s] Закрытие потоков.\n", CLR_CYAN, CLR_RESET);
    printf("[%s  OK  %s] Файл %s записан!\n", CLR_GREEN, CLR_RESET, argv[2]);
    return 0;
}
