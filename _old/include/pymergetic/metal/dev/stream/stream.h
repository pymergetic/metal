/*
 * Metal byte streams — endpoints + stdio attach (guest/host dual ABI).
 * See docs/IO.md.
 *
 * impl: common — src/pymergetic/metal/dev/stream/stream.c
 */
#ifndef PYMERGETIC_METAL_DEV_STREAM_STREAM_H_
#define PYMERGETIC_METAL_DEV_STREAM_STREAM_H_

#include <stdint.h>

#include "pymergetic/metal/runtime/async/async.h"
#include "pymergetic/metal/shell/ui/ui.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_STREAM_WASI_MODULE "pymergetic.metal.stream"

typedef uint32_t pm_metal_stream_h;

#define PM_METAL_STREAM_INVALID 0u

/** PTY attrs (sync surface). Flag values match Linux termios for Dropbear. */
#define PM_METAL_STREAM_NCCS 32u

#define PM_METAL_STREAM_VINTR  0u
#define PM_METAL_STREAM_VQUIT  1u
#define PM_METAL_STREAM_VERASE 2u
#define PM_METAL_STREAM_VKILL  3u
#define PM_METAL_STREAM_VEOF   4u
#define PM_METAL_STREAM_VTIME  5u
#define PM_METAL_STREAM_VMIN   6u
#define PM_METAL_STREAM_VSUSP  10u

#define PM_METAL_STREAM_ICRNL  0000400u
#define PM_METAL_STREAM_IXON   0002000u
#define PM_METAL_STREAM_OPOST  0000001u
#define PM_METAL_STREAM_ONLCR  0000004u
#define PM_METAL_STREAM_CS8    0000060u
#define PM_METAL_STREAM_CREAD  0000200u
#define PM_METAL_STREAM_HUPCL  0002000u
#define PM_METAL_STREAM_ISIG   0000001u
#define PM_METAL_STREAM_ICANON 0000002u
#define PM_METAL_STREAM_ECHO   0000010u
#define PM_METAL_STREAM_ECHOE  0000020u
#define PM_METAL_STREAM_ECHOK  0000040u
#define PM_METAL_STREAM_IEXTEN 0100000u

typedef struct {
  uint32_t iflag;
  uint32_t oflag;
  uint32_t cflag;
  uint32_t lflag;
  uint8_t  cc[PM_METAL_STREAM_NCCS];
  uint32_t ispeed;
  uint32_t ospeed;
} pm_metal_stream_termios_t;

typedef struct {
  uint16_t row;
  uint16_t col;
  uint16_t xpixel;
  uint16_t ypixel;
} pm_metal_stream_winsize_t;

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_STREAM_IMPORT(name) PM_METAL_WASI_IMPORT(PM_METAL_STREAM_WASI_MODULE, name)

extern pm_metal_stream_h pm_metal_stream_open_uart(void)
  PM_METAL_STREAM_IMPORT(pm_metal_stream_open_uart);
extern pm_metal_stream_h pm_metal_stream_open_ui_tab(pm_metal_ui_handle_t tab)
  PM_METAL_STREAM_IMPORT(pm_metal_stream_open_ui_tab);
extern int32_t pm_metal_stream_pipe(uint32_t read_out, uint32_t write_out)
  PM_METAL_STREAM_IMPORT(pm_metal_stream_pipe);
extern int32_t pm_metal_stream_pty(uint32_t master_out, uint32_t slave_out)
  PM_METAL_STREAM_IMPORT(pm_metal_stream_pty);
extern uint32_t pm_metal_stream_write(pm_metal_stream_h h, uint32_t ptr, uint32_t len)
  PM_METAL_STREAM_IMPORT(pm_metal_stream_write);
extern pm_metal_async_handle_t pm_metal_stream_read(pm_metal_stream_h h, uint32_t ptr, uint32_t len)
  PM_METAL_STREAM_IMPORT(pm_metal_stream_read);
extern pm_metal_async_handle_t pm_metal_stream_drain(pm_metal_stream_h h)
  PM_METAL_STREAM_IMPORT(pm_metal_stream_drain);
extern void pm_metal_stream_close(pm_metal_stream_h h)
  PM_METAL_STREAM_IMPORT(pm_metal_stream_close);
extern int32_t pm_metal_stdio_attach(pm_metal_stream_h in,
                                     pm_metal_stream_h out,
                                     pm_metal_stream_h err)
  PM_METAL_STREAM_IMPORT(pm_metal_stdio_attach);
#else
pm_metal_stream_h pm_metal_stream_open_uart(void);
pm_metal_stream_h pm_metal_stream_open_ui_tab(pm_metal_ui_handle_t tab);
int               pm_metal_stream_pipe(pm_metal_stream_h *read_end, pm_metal_stream_h *write_end);
int               pm_metal_stream_pty(pm_metal_stream_h *master, pm_metal_stream_h *slave);
uint32_t          pm_metal_stream_write(pm_metal_stream_h h, const void *ptr, uint32_t len);
pm_metal_async_handle_t pm_metal_stream_read(pm_metal_stream_h h, void *ptr, uint32_t len);
/** Host-only non-blocking read from ring (pipe/pty). 0 if empty. */
uint32_t                pm_metal_stream_try_read(pm_metal_stream_h h, void *ptr, uint32_t len);
pm_metal_async_handle_t pm_metal_stream_drain(pm_metal_stream_h h);
void                    pm_metal_stream_close(pm_metal_stream_h h);
int pm_metal_stdio_attach(pm_metal_stream_h in, pm_metal_stream_h out, pm_metal_stream_h err);

/** PTY termios/winsize (shared by master+slave). 0 ok, -1 if not a PTY. */
int pm_metal_stream_termios_get(pm_metal_stream_h h, pm_metal_stream_termios_t *out);
int pm_metal_stream_termios_set(pm_metal_stream_h h, const pm_metal_stream_termios_t *in);
int pm_metal_stream_winsize_get(pm_metal_stream_h h, pm_metal_stream_winsize_t *out);
int pm_metal_stream_winsize_set(pm_metal_stream_h h, const pm_metal_stream_winsize_t *in);
/** Bytes waiting in the stream's own RX ring (pipe/pty/uart). */
uint32_t pm_metal_stream_pending(pm_metal_stream_h h);

/** PTY termios/winsize (shared by master+slave). 0 ok, -1 if not a PTY. */
int pm_metal_stream_termios_get(pm_metal_stream_h h, pm_metal_stream_termios_t *out);
int pm_metal_stream_termios_set(pm_metal_stream_h h, const pm_metal_stream_termios_t *in);
int pm_metal_stream_winsize_get(pm_metal_stream_h h, pm_metal_stream_winsize_t *out);
int pm_metal_stream_winsize_set(pm_metal_stream_h h, const pm_metal_stream_winsize_t *in);
/** Bytes waiting in the stream's own RX ring (pipe/pty/uart). */
uint32_t pm_metal_stream_pending(pm_metal_stream_h h);

/** Host helpers for shell / WASI stdout migration. */
pm_metal_stream_h pm_metal_stdio_out(void);
pm_metal_stream_h pm_metal_stdio_err(void);
pm_metal_stream_h pm_metal_stdio_in(void);
/** Push bytes into attached stdio_in ring (ConIn / shell). */
uint32_t pm_metal_stream_feed_stdin(const void *ptr, uint32_t len);
/** Write a NUL-terminated line (adds '\\n'). Returns bytes accepted. */
uint32_t pm_metal_stream_write_line(pm_metal_stream_h h, const char *line);

int  pm_metal_stream_native_register(void);
void pm_metal_stream_bind_inst(void *module_inst);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_STREAM_STREAM_H_ */
