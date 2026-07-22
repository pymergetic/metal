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

#if defined(__wasm__)
#include "pymergetic/metal/wasi.h"
#define PM_METAL_STREAM_IMPORT(name) \
	PM_METAL_WASI_IMPORT(PM_METAL_STREAM_WASI_MODULE, name)

extern pm_metal_stream_h pm_metal_stream_open_uart(void)
	PM_METAL_STREAM_IMPORT(pm_metal_stream_open_uart);
extern pm_metal_stream_h pm_metal_stream_open_ui_tab(pm_metal_ui_handle_t tab)
	PM_METAL_STREAM_IMPORT(pm_metal_stream_open_ui_tab);
extern int32_t pm_metal_stream_pipe(uint32_t read_out, uint32_t write_out)
	PM_METAL_STREAM_IMPORT(pm_metal_stream_pipe);
extern int32_t pm_metal_stream_pty(uint32_t master_out, uint32_t slave_out)
	PM_METAL_STREAM_IMPORT(pm_metal_stream_pty);
extern uint32_t pm_metal_stream_write(pm_metal_stream_h h, uint32_t ptr,
				     uint32_t len)
	PM_METAL_STREAM_IMPORT(pm_metal_stream_write);
extern pm_metal_async_handle_t pm_metal_stream_read(pm_metal_stream_h h,
						    uint32_t ptr, uint32_t len)
	PM_METAL_STREAM_IMPORT(pm_metal_stream_read);
extern pm_metal_async_handle_t pm_metal_stream_drain(pm_metal_stream_h h)
	PM_METAL_STREAM_IMPORT(pm_metal_stream_drain);
extern void pm_metal_stream_close(pm_metal_stream_h h)
	PM_METAL_STREAM_IMPORT(pm_metal_stream_close);
extern int32_t pm_metal_stdio_attach(pm_metal_stream_h in, pm_metal_stream_h out,
				     pm_metal_stream_h err)
	PM_METAL_STREAM_IMPORT(pm_metal_stdio_attach);
#else
pm_metal_stream_h pm_metal_stream_open_uart(void);
pm_metal_stream_h pm_metal_stream_open_ui_tab(pm_metal_ui_handle_t tab);
int pm_metal_stream_pipe(pm_metal_stream_h *read_end, pm_metal_stream_h *write_end);
int pm_metal_stream_pty(pm_metal_stream_h *master, pm_metal_stream_h *slave);
uint32_t pm_metal_stream_write(pm_metal_stream_h h, const void *ptr, uint32_t len);
pm_metal_async_handle_t pm_metal_stream_read(pm_metal_stream_h h, void *ptr,
					     uint32_t len);
pm_metal_async_handle_t pm_metal_stream_drain(pm_metal_stream_h h);
void pm_metal_stream_close(pm_metal_stream_h h);
int pm_metal_stdio_attach(pm_metal_stream_h in, pm_metal_stream_h out,
			  pm_metal_stream_h err);

/** Host helpers for shell / WASI stdout migration. */
pm_metal_stream_h pm_metal_stdio_out(void);
pm_metal_stream_h pm_metal_stdio_err(void);
pm_metal_stream_h pm_metal_stdio_in(void);
/** Push bytes into attached stdio_in ring (ConIn / shell). */
uint32_t pm_metal_stream_feed_stdin(const void *ptr, uint32_t len);
/** Write a NUL-terminated line (adds '\\n'). Returns bytes accepted. */
uint32_t pm_metal_stream_write_line(pm_metal_stream_h h, const char *line);

int pm_metal_stream_native_register(void);
void pm_metal_stream_bind_inst(void *module_inst);
#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_STREAM_STREAM_H_ */
