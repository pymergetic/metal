/*
 * Dropbear session control for Metal sshd (host-only).
 */
#ifndef PYMERGETIC_METAL_NET_SSH_DROPBEAR_H_
#define PYMERGETIC_METAL_NET_SSH_DROPBEAR_H_

#include <stdint.h>

#include <pymergetic/metal/dev/stream/__init__.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Ensure host keys (DELAY_HOSTKEY — generate on first KEX). 0 ok. */
int32_t metal_dropbear_ensure_hostkeys(void);

/**
 * Start a Dropbear server session on an accepted TCP stream handle.
 * Returns opaque cookie, or 0 on failure.
 */
uint32_t metal_dropbear_session_start(uint32_t stream_h, pm_metal_stream_h pty_master,
                                      pm_metal_stream_h pty_slave);

/** One coop step: poll Dropbear once. Returns 0 continue, -1 session ended. */
int32_t metal_dropbear_session_poll(uint32_t sess);

void metal_dropbear_session_close(uint32_t sess);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_NET_SSH_DROPBEAR_H_ */
