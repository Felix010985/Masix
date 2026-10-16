/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel — bootstrap userland
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * Минимальные строковые/памятные функции для свободностоящего
 * юзерленда. Никакой libc: только то, что реально нужно
 * init и shell. Компилятор может подставлять вызовы memset/memcpy
 * при копировании структур - они реализованы здесь.
 */
#ifndef BOOTSTRAP_LIB_H
#define BOOTSTRAP_LIB_H

static inline void* memset(void* dst, int value, unsigned int n) {
    unsigned char* p = (unsigned char*)dst;
    for (unsigned int i = 0; i < n; i++) p[i] = (unsigned char)value;
    return dst;
}

static inline void* memcpy(void* dst, const void* src, unsigned int n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    for (unsigned int i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

static inline unsigned int strlen(const char* s) {
    unsigned int n = 0;
    while (s[n]) n++;
    return n;
}

static inline int strcmp(const char* a, const char* b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

static inline int strncmp(const char* a, const char* b, unsigned int n) {
    for (unsigned int i = 0; i < n; i++) {
        if (a[i] != b[i]) return (unsigned char)a[i] - (unsigned char)b[i];
        if (!a[i]) return 0;
    }
    return 0;
}

static inline char* strcpy(char* dst, const char* src) {
    unsigned int i = 0;
    while ((dst[i] = src[i])) i++;
    return dst;
}

static inline char* strcat(char* dst, const char* src) {
    unsigned int i = strlen(dst);
    strcpy(dst + i, src);
    return dst;
}

static inline int atoi(const char* s) {
    int n = 0, sign = 1;
    while (*s == ' ' || *s == '\t') s++;
    if (*s == '-') { sign = -1; s++; }
    else if (*s == '+') s++;
    while (*s >= '0' && *s <= '9') n = n * 10 + (*s++ - '0');
    return n * sign;
}

#endif /* BOOTSTRAP_LIB_H */
