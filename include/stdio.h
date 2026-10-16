#include <drivers/vga.h>
#include <stdarg.h>

void print_str(const char* s); // стринги
void print_num(int n);

void printf(const char* fmt, ...);
int scanf(const char *fmt, ...);
char *fgets(char *buf, int size, void *unused_stream);
