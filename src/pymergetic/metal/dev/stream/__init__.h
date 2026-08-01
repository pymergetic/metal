/*
 * Metal byte streams — pipe / pty rings (host). Async read/drain.
 * UART/UI tab backends omitted until those modules exist in live tree.
 */
#ifndef PYMERGETIC_METAL_DEV_STREAM_H_
#define PYMERGETIC_METAL_DEV_STREAM_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint32_t pm_metal_stream_h;

#define PM_METAL_STREAM_INVALID 0u

/** PTY attrs (sync). Flag values match Linux termios for Dropbear. */
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

int32_t pm_metal_stream_pipe(pm_metal_stream_h *read_end, pm_metal_stream_h *write_end);
int32_t pm_metal_stream_pty(pm_metal_stream_h *master, pm_metal_stream_h *slave);
uint32_t pm_metal_stream_write(pm_metal_stream_h h, const void *ptr, uint32_t len);
/** Non-blocking read from ring (pipe/pty). 0 if empty. */
uint32_t pm_metal_stream_try_read(pm_metal_stream_h h, void *ptr, uint32_t len);
/** Async read; result_u32 = bytes. Handle 0 = fail. */
uint32_t pm_metal_stream_read(pm_metal_stream_h h, void *ptr, uint32_t len);
/** Async wait until peer ring has space (pipe/pty write end). */
uint32_t pm_metal_stream_drain(pm_metal_stream_h h);
void pm_metal_stream_close(pm_metal_stream_h h);

int32_t pm_metal_stream_termios_get(pm_metal_stream_h h, pm_metal_stream_termios_t *out);
int32_t pm_metal_stream_termios_set(pm_metal_stream_h h, const pm_metal_stream_termios_t *in);
int32_t pm_metal_stream_winsize_get(pm_metal_stream_h h, pm_metal_stream_winsize_t *out);
int32_t pm_metal_stream_winsize_set(pm_metal_stream_h h, const pm_metal_stream_winsize_t *in);
uint32_t pm_metal_stream_pending(pm_metal_stream_h h);

int32_t pm_metal_stdio_attach(pm_metal_stream_h in, pm_metal_stream_h out, pm_metal_stream_h err);
pm_metal_stream_h pm_metal_stdio_in(void);
pm_metal_stream_h pm_metal_stdio_out(void);
pm_metal_stream_h pm_metal_stdio_err(void);
uint32_t pm_metal_stream_feed_stdin(const void *ptr, uint32_t len);
uint32_t pm_metal_stream_write_line(pm_metal_stream_h h, const char *line);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_STREAM_H_ */
