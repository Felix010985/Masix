#ifndef TSS_H
#define TSS_H

#include <stdint.h>

/* Стек ядра, используемый TSS до переключения на task_schedule */
extern uint8_t tss_kernel_stack[4096];

void tss_write(int num, uint16_t ss0, uint32_t esp0);

#endif
