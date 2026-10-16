#ifndef TTY_H
#define TTY_H

#include <stdint.h>
#include <vfs.h>

void tty_write_char(char c);
void tty_backspace(void);
void tty_draw_cursor(void);
void tty_erase_cursor(void);

/* Операции консольного устройства, регистрируемые в реестре VFS */
int32_t tty_read(vfs_node_t *node, uint32_t offset, uint32_t size, uint8_t *buffer);
int32_t tty_write(vfs_node_t *node, uint32_t offset, uint32_t size, const uint8_t *buffer);

void tty_init(void);

uint32_t tty_width(void);
uint32_t tty_height(void);

#endif
