/*
 * Port — zephyr pipe bind. Userspace ring-buffer pipes with high fd numbers
 * (no POSIX pipe(2) on this target). file.c recognizes these fds for
 * readv/writev/close so WASI stdio chaining works.
 */
#include "pymergetic/metal/port/pipe.h"

#include <string.h>

#include <zephyr/kernel.h>

#define PM_METAL_PIPE_FD_BASE 1000
#define PM_METAL_PIPE_MAX 4
#define PM_METAL_PIPE_BUF 4096

typedef struct pm_metal_pipe {
	uint8_t buf[PM_METAL_PIPE_BUF];
	size_t r;
	size_t w;
	size_t len;
	int read_open;
	int write_open;
	struct k_mutex lock;
	struct k_condvar readable;
	struct k_condvar writable;
	int used;
} pm_metal_pipe_t;

static pm_metal_pipe_t g_pm_metal_pipes[PM_METAL_PIPE_MAX];

/* Protects slot .used allocation; always acquire before per-pipe lock. */
K_MUTEX_DEFINE(g_pm_metal_pipe_table_lock);

int pm_metal_port_pipe_is_ours(int fd)
{
	int idx;
	int used;

	if (fd < PM_METAL_PIPE_FD_BASE) {
		return 0;
	}
	idx = (fd - PM_METAL_PIPE_FD_BASE) / 2;
	if (idx < 0 || idx >= PM_METAL_PIPE_MAX) {
		return 0;
	}
	k_mutex_lock(&g_pm_metal_pipe_table_lock, K_FOREVER);
	used = g_pm_metal_pipes[idx].used;
	k_mutex_unlock(&g_pm_metal_pipe_table_lock);
	return used;
}

int pm_metal_port_pipe_is_read_end(int fd)
{
	return pm_metal_port_pipe_is_ours(fd) && ((fd - PM_METAL_PIPE_FD_BASE) % 2) == 0;
}

ssize_t pm_metal_port_pipe_read(int fd, void *buf, size_t n)
{
	int idx = (fd - PM_METAL_PIPE_FD_BASE) / 2;
	pm_metal_pipe_t *p;
	size_t got = 0;
	uint8_t *out = buf;

	if (!pm_metal_port_pipe_is_read_end(fd) || !buf) {
		return -1;
	}
	p = &g_pm_metal_pipes[idx];
	k_mutex_lock(&p->lock, K_FOREVER);
	while (p->len == 0 && p->write_open) {
		k_condvar_wait(&p->readable, &p->lock, K_FOREVER);
	}
	while (got < n && p->len > 0) {
		out[got++] = p->buf[p->r];
		p->r = (p->r + 1) % PM_METAL_PIPE_BUF;
		p->len--;
	}
	k_condvar_signal(&p->writable);
	k_mutex_unlock(&p->lock);
	return (ssize_t)got;
}

ssize_t pm_metal_port_pipe_write(int fd, const void *buf, size_t n)
{
	int idx = (fd - PM_METAL_PIPE_FD_BASE) / 2;
	pm_metal_pipe_t *p;
	size_t put = 0;
	const uint8_t *in = buf;

	if (pm_metal_port_pipe_is_read_end(fd) || !pm_metal_port_pipe_is_ours(fd) || !buf) {
		return -1;
	}
	p = &g_pm_metal_pipes[idx];
	k_mutex_lock(&p->lock, K_FOREVER);
	if (!p->read_open) {
		k_mutex_unlock(&p->lock);
		return -1;
	}
	while (put < n) {
		while (p->len == PM_METAL_PIPE_BUF && p->read_open) {
			k_condvar_wait(&p->writable, &p->lock, K_FOREVER);
		}
		if (!p->read_open) {
			break;
		}
		p->buf[p->w] = in[put++];
		p->w = (p->w + 1) % PM_METAL_PIPE_BUF;
		p->len++;
		k_condvar_signal(&p->readable);
	}
	k_mutex_unlock(&p->lock);
	return (ssize_t)put;
}

void pm_metal_port_pipe_close_fd(int fd)
{
	int idx;
	pm_metal_pipe_t *p;
	int is_read;

	if (fd < PM_METAL_PIPE_FD_BASE) {
		return;
	}
	idx = (fd - PM_METAL_PIPE_FD_BASE) / 2;
	if (idx < 0 || idx >= PM_METAL_PIPE_MAX) {
		return;
	}
	is_read = ((fd - PM_METAL_PIPE_FD_BASE) % 2) == 0;
	p = &g_pm_metal_pipes[idx];

	/* Deadlock-safe order: table_lock then pipe_lock. */
	k_mutex_lock(&g_pm_metal_pipe_table_lock, K_FOREVER);
	if (!p->used) {
		k_mutex_unlock(&g_pm_metal_pipe_table_lock);
		return;
	}
	k_mutex_lock(&p->lock, K_FOREVER);
	if (is_read) {
		p->read_open = 0;
	} else {
		p->write_open = 0;
	}
	k_condvar_broadcast(&p->readable);
	k_condvar_broadcast(&p->writable);
	if (!p->read_open && !p->write_open) {
		p->used = 0;
	}
	k_mutex_unlock(&p->lock);
	k_mutex_unlock(&g_pm_metal_pipe_table_lock);
}

int pm_metal_port_pipe(int64_t *out_read_fd, int64_t *out_write_fd)
{
	int i;

	if (!out_read_fd || !out_write_fd) {
		return -1;
	}
	k_mutex_lock(&g_pm_metal_pipe_table_lock, K_FOREVER);
	for (i = 0; i < PM_METAL_PIPE_MAX; i++) {
		if (!g_pm_metal_pipes[i].used) {
			memset(&g_pm_metal_pipes[i], 0, sizeof(g_pm_metal_pipes[i]));
			k_mutex_init(&g_pm_metal_pipes[i].lock);
			k_condvar_init(&g_pm_metal_pipes[i].readable);
			k_condvar_init(&g_pm_metal_pipes[i].writable);
			g_pm_metal_pipes[i].read_open = 1;
			g_pm_metal_pipes[i].write_open = 1;
			g_pm_metal_pipes[i].used = 1;
			*out_read_fd = PM_METAL_PIPE_FD_BASE + i * 2;
			*out_write_fd = PM_METAL_PIPE_FD_BASE + i * 2 + 1;
			k_mutex_unlock(&g_pm_metal_pipe_table_lock);
			return 0;
		}
	}
	k_mutex_unlock(&g_pm_metal_pipe_table_lock);
	return -1;
}

void pm_metal_port_close(int64_t fd)
{
	if (fd < 0) {
		return;
	}
	if (pm_metal_port_pipe_is_ours((int)fd)) {
		pm_metal_port_pipe_close_fd((int)fd);
	}
}
