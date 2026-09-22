/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Masix Kernel
 * Copyright (C) 2026, FelixProfi. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */
// Kernel LibK
#include <drivers/vga.h>
#include <alloc.h>
#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

extern void tty_write_char(char c);
extern char keyboard_getc(void);
unsigned int strlen(const char* s);

#ifndef NULL
#define NULL ((void*)0)
#endif

#define MAX_INPUT 128

void* memcpy(void* dst, const void* src, unsigned int n) {
    unsigned char* d = dst;
    const unsigned char* s = src;
    for (unsigned int i = 0; i < n; i++) d[i] = s[i];
    return dst;
}

int strncmp(const char *s1, const char *s2, register size_t n) {
    register unsigned char u1, u2;

    while (n-- > 0)
    {
        u1 = (unsigned char) *s1++;
        u2 = (unsigned char) *s2++;
        if (u1 != u2)
            return u1 - u2;
        if (u1 == '\0')
            return 0;
    }
    return 0;
}

char* strncpy(char* dst, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n && src[i] != '\0'; i++) {
        dst[i] = src[i];
    }
    for (; i < n; i++) {
        dst[i] = '\0';
    }
    return dst;
}

char* strcat(char* dst, const char* src) {
    char* p = dst;
    while (*p) p++; // Находим конец строки dst
    while (*src) {
        *p++ = *src++;
    }
    *p = '\0';
    return dst;
}

char* strncat(char* dst, const char* src, size_t n) {
    char* p = dst;
    while (*p) p++; // Находим конец строки dst
    while (n > 0 && *src) {
        *p++ = *src++;
        n--;
    }
    *p = '\0';
    return dst;
}

char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) return (char*)s;
        s++;
    }
    if ((char)c == '\0') return (char*)s;
    return NULL;
}

char* strrchr(const char* s, int c) {
    const char* last = NULL;
    while (*s) {
        if (*s == (char)c) last = s;
        s++;
    }
    if ((char)c == '\0') return (char*)s;
    return (char*)last;
}

char* strdup(const char* s) {
    unsigned int len = strlen(s) + 1;
    char* res = alloc(len);
    if (res) memcpy(res, s, len);
    return res;
}

void* memset(void* dst, int value, unsigned int n) {
    unsigned char* d = dst;
    for (unsigned int i = 0; i < n; i++) d[i] = (unsigned char)value;
    return dst;
}

int memcmp(const void *s1, const void *s2, size_t n) {
    const unsigned char *p1 = (const unsigned char *)s1;
    const unsigned char *p2 = (const unsigned char *)s2;

    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] - p2[i];
        }
    }

    return 0;
}

unsigned int strlen(const char* s) {
    unsigned int i = 0;
    while (s[i]) i++;
    return i;
}

char* strcpy(char* dst, const char* src) {
    unsigned int i = 0;
    while (src[i]) { dst[i] = src[i]; i++; }
    dst[i] = 0;
    return dst;
}

int strcmp(const char* a, const char* b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return (unsigned char)(*a) - (unsigned char)(*b);
}

void itoa(int value, char* str, int base) {
    char buf[33];
    int i = 0, is_negative = 0;
    if (value == 0) { str[0]='0'; str[1]=0; return; }
    if (base == 10 && value < 0) { is_negative = 1; value = -value; }
    while (value) {
        int rem = value % base;
        buf[i++] = (rem < 10) ? ('0'+rem) : ('A'+rem-10);
        value /= base;
    }
    if (is_negative) buf[i++] = '-';
    int j=0;
    while (i>0) str[j++] = buf[--i];
    str[j]=0;
}

int atoi(const char* str) {
    int res=0, sign=1, i=0;
    if (str[0]=='-') { sign=-1; i=1; }
    for (; str[i]; i++) res = res*10 + (str[i]-'0');
    return res*sign;
}

void print_str(const char* s) {
    while(*s) {
        tty_write_char(*s++);
    }
}

void print_num(int n) {
    char buf[16];
    itoa(n, buf, 10);
    print_str(buf);
}

static void internal_kernel_read_line(char* buf, int max_len) {
    int read_bytes = 0;
    while (read_bytes < max_len - 1) {
        char c = keyboard_getc();
        if (c == '\n' || c == '\r') {
            buf[read_bytes++] = '\n';
            tty_write_char('\n');
            break;
        } else if (c == '\b') {
            if (read_bytes > 0) {
                read_bytes--;
                tty_write_char('\b');
            }
        } else {
            buf[read_bytes++] = c;
            tty_write_char(c);
        }
    }
    buf[read_bytes] = '\0';
}

void printf(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);

    for (int i = 0; fmt[i]; i++) {
        if (fmt[i] == '%' && fmt[i+1]) {
            i++;
            if (fmt[i] == 's') {
                const char* s = va_arg(args, const char*);
                print_str(s);
            } else if (fmt[i] == 'd') {
                int num = va_arg(args, int);
                char buf[32];
                itoa(num, buf, 10);
                print_str(buf);
            } else if (fmt[i] == 'x') {
                int num = va_arg(args, int);
                char buf[32];
                itoa(num, buf, 16);
                print_str(buf);
            } else if (fmt[i] == 'c') {
                char c = (char)va_arg(args, int);
                tty_write_char(c);
            } else if (fmt[i] == '%') {
                tty_write_char('%');
            }
        } else {
            tty_write_char(fmt[i]);
        }
    }

    va_end(args);
}

int scanf(const char *fmt, ...) {
    char buffer[MAX_INPUT];
    internal_kernel_read_line(buffer, MAX_INPUT);

    const char *p = buffer;
    va_list args;
    va_start(args, fmt);

    int assigned = 0;

    for (int i = 0; fmt[i]; i++) {
        if (fmt[i] == '%' && fmt[i+1]) {
            i++;
            if (fmt[i] == 'd') {
                int *out = va_arg(args, int *);
                while (*p && (*p == ' ' || *p == '\t')) p++;
                int sign = 1, val = 0;
                if (*p == '-') { sign = -1; p++; }
                else if (*p == '+') { p++; }
                int found = 0;
                while (*p >= '0' && *p <= '9') {
                    val = val * 10 + (*p - '0');
                    p++;
                    found = 1;
                }
                if (found) { *out = val * sign; assigned++; }
            } else if (fmt[i] == 'u') {
                unsigned int *out = va_arg(args, unsigned int *);
                while (*p && (*p == ' ' || *p == '\t')) p++;
                unsigned int val = 0;
                int found = 0;
                while (*p >= '0' && *p <= '9') {
                    val = val * 10 + (*p - '0');
                    p++;
                    found = 1;
                }
                if (found) { *out = val; assigned++; }
            } else if (fmt[i] == 'x') {
                unsigned int *out = va_arg(args, unsigned int *);
                while (*p && (*p == ' ' || *p == '\t')) p++;
                unsigned int val = 0;
                int found = 0;
                while ((*p >= '0' && *p <= '9') ||
                    (*p >= 'a' && *p <= 'f') ||
                    (*p >= 'A' && *p <= 'F')) {
                    char c = *p;
                int digit;
                if (c >= '0' && c <= '9') digit = c - '0';
                else if (c >= 'a' && c <= 'f') digit = c - 'a' + 10;
                else digit = c - 'A' + 10;
                val = val * 16 + digit;
                    p++;
                    found = 1;
                    }
                    if (found) { *out = val; assigned++; }
            } else if (fmt[i] == 's') {
                char *out = va_arg(args, char *);
                while (*p && (*p == ' ' || *p == '\t')) p++;
                if (*p) {
                    while (*p && *p != ' ' && *p != '\t' && *p != '\n') {
                        *out++ = *p++;
                    }
                    *out = 0;
                    assigned++;
                }
            } else if (fmt[i] == 'c') {
                char *out = va_arg(args, char *);
                if (*p) { *out = *p++; assigned++; }
            }
        }
    }

    va_end(args);
    return assigned;
}

char *fgets(char *buf, int size, void *unused_stream) {
    (void)unused_stream;
    if (!buf || size <= 0) return NULL;

    internal_kernel_read_line(buf, size);

    for (int i = 0; i < size; i++) {
        if (buf[i] == '\n' || buf[i] == '\r') {
            buf[i] = 0;
            break;
        }
        if (buf[i] == 0) break;
    }

    return buf;
}
