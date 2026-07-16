/*
 * Pipe helpers shared with WASI file.c (plat-private).
 */
#ifndef PYMERGETIC_METAL_PORT_PIPE_ZEPHYR_H_
#define PYMERGETIC_METAL_PORT_PIPE_ZEPHYR_H_

#include <sys/types.h>

/* impl: zephyr — src/zephyr/pymergetic/metal/port/pipe.c */
int pm_metal_port_pipe_is_ours(int fd);
int pm_metal_port_pipe_is_read_end(int fd);
ssize_t pm_metal_port_pipe_read(int fd, void *buf, size_t n);
ssize_t pm_metal_port_pipe_write(int fd, const void *buf, size_t n);
void pm_metal_port_pipe_close_fd(int fd);

#endif
