/*
 * Dropbear session control for Metal sshd (host-only).
 */
#ifndef PYMERGETIC_METAL_DEV_NET_SSH_DROPBEAR_H_
#define PYMERGETIC_METAL_DEV_NET_SSH_DROPBEAR_H_

#include <stdint.h>

#include <pymergetic/metal/net/ip/ip.h>
#include <pymergetic/metal/dev/stream/stream.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Ensure host keys under /etc/ssh/ (generate ed25519 if missing). 0 ok. */
int32_t metal_dropbear_ensure_hostkeys(void);

/**
 * Start a Dropbear server session on an accepted TCP sock.
 * Returns opaque cookie, or 0 on failure. PTY slave used for shell after auth.
 */
uint32_t metal_dropbear_session_start(pm_metal_net_ip_sock_h sock,
                                      pm_metal_stream_h      pty_master,
                                      pm_metal_stream_h      pty_slave);

/** One coop step: poll Dropbear once. Returns 0 continue, -1 session ended. */
int32_t metal_dropbear_session_poll(uint32_t sess);

void metal_dropbear_session_close(uint32_t sess);

#ifdef __cplusplus
}
#endif

#endif
