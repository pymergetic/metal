/** @file
  Metal byte streams — ui_tab / uart / pipe / pty. (impl: efi|bios)
**/
#include <pymergetic/metal/dev/stream/stream.h>
#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <runtime/coro/coro.h>
#include <runtime/mem/mem.h>
#include <runtime/time/time.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/PrintLib.h>

#include "wasm_export.h"

#ifndef PM_METAL_STREAM_MAX
#define PM_METAL_STREAM_MAX  32u
#endif

#ifndef PM_METAL_STREAM_RING
#define PM_METAL_STREAM_RING  4096u
#endif

typedef enum {
  PM_METAL_STREAM_KIND_NONE = 0,
  PM_METAL_STREAM_KIND_UART,
  PM_METAL_STREAM_KIND_UI_TAB,
  PM_METAL_STREAM_KIND_PIPE,
  PM_METAL_STREAM_KIND_PTY
} pm_metal_stream_kind_t;

typedef struct {
  INT32                   used;
  pm_metal_stream_kind_t  kind;
  pm_metal_ui_handle_t    tab;
  UINT32                  peer; /* pipe/pty other end */
  UINT8                  *ring;
  UINT32                  rhead;
  UINT32                  rtail;
  UINT32                  rcap;
} pm_metal_stream_slot_t;

STATIC pm_metal_stream_slot_t  mSlots[PM_METAL_STREAM_MAX + 1];
STATIC pm_metal_stream_h       mStdIn;
STATIC pm_metal_stream_h       mStdOut;
STATIC pm_metal_stream_h       mStdErr;
STATIC wasm_module_inst_t      mStreamInst;

void
pm_metal_stream_bind_inst (
  VOID  *module_inst
  )
{
  mStreamInst = (wasm_module_inst_t)module_inst;
}

STATIC
UINT32
MetalStreamAlloc (
  pm_metal_stream_kind_t  kind
  )
{
  UINT32  i;

  for (i = 1; i <= PM_METAL_STREAM_MAX; i++) {
    if (!mSlots[i].used) {
      ZeroMem (&mSlots[i], sizeof (mSlots[i]));
      mSlots[i].used = 1;
      mSlots[i].kind = kind;
      return i;
    }
  }

  return 0;
}

STATIC
INT32
MetalStreamRingAlloc (
  UINT32  h
  )
{
  if (h == 0 || h > PM_METAL_STREAM_MAX) {
    return -1;
  }

  if (mSlots[h].ring != NULL) {
    return 0;
  }

  mSlots[h].ring = (UINT8 *)pm_metal_mem_alloc (
                              PM_METAL_STREAM_RING,
                              PM_METAL_MEM_HEAP,
                              PM_METAL_MEM_ID_NONE
                              );
  if (mSlots[h].ring == NULL) {
    return -1;
  }

  mSlots[h].rcap  = PM_METAL_STREAM_RING;
  mSlots[h].rhead = 0;
  mSlots[h].rtail = 0;
  return 0;
}

STATIC
UINT32
MetalStreamRingUsed (
  UINT32  h
  )
{
  pm_metal_stream_slot_t  *s;

  s = &mSlots[h];
  if (s->rhead >= s->rtail) {
    return s->rhead - s->rtail;
  }

  return s->rcap - (s->rtail - s->rhead);
}

STATIC
UINT32
MetalStreamRingSpace (
  UINT32  h
  )
{
  return mSlots[h].rcap - MetalStreamRingUsed (h) - 1u;
}

STATIC
UINT32
MetalStreamRingPut (
  UINT32       h,
  CONST UINT8  *data,
  UINT32        len
  )
{
  UINT32  n;
  UINT32  i;

  if (h == 0 || data == NULL || len == 0 || MetalStreamRingAlloc (h) != 0) {
    return 0;
  }

  n = MetalStreamRingSpace (h);
  if (n > len) {
    n = len;
  }

  for (i = 0; i < n; i++) {
    mSlots[h].ring[mSlots[h].rhead] = data[i];
    mSlots[h].rhead = (mSlots[h].rhead + 1u) % mSlots[h].rcap;
  }

  return n;
}

STATIC
UINT32
MetalStreamRingGet (
  UINT32  h,
  UINT8  *data,
  UINT32  len
  )
{
  UINT32  n;
  UINT32  i;
  UINT32  used;

  if (h == 0 || data == NULL || len == 0 || mSlots[h].ring == NULL) {
    return 0;
  }

  used = MetalStreamRingUsed (h);
  n    = used < len ? used : len;
  for (i = 0; i < n; i++) {
    data[i] = mSlots[h].ring[mSlots[h].rtail];
    mSlots[h].rtail = (mSlots[h].rtail + 1u) % mSlots[h].rcap;
  }

  return n;
}

pm_metal_stream_h
pm_metal_stream_open_uart (
  VOID
  )
{
  UINT32  h;

  h = MetalStreamAlloc (PM_METAL_STREAM_KIND_UART);
  if (h == 0) {
    return PM_METAL_STREAM_INVALID;
  }

  if (MetalStreamRingAlloc (h) != 0) {
    pm_metal_stream_close (h);
    return PM_METAL_STREAM_INVALID;
  }

  return (pm_metal_stream_h)h;
}

pm_metal_stream_h
pm_metal_stream_open_ui_tab (
  pm_metal_ui_handle_t  tab
  )
{
  UINT32  h;

  if (tab == PM_METAL_UI_HANDLE_INVALID) {
    return PM_METAL_STREAM_INVALID;
  }

  h = MetalStreamAlloc (PM_METAL_STREAM_KIND_UI_TAB);
  if (h == 0) {
    return PM_METAL_STREAM_INVALID;
  }

  mSlots[h].tab = tab;
  return (pm_metal_stream_h)h;
}

int
pm_metal_stream_pipe (
  pm_metal_stream_h  *read_end,
  pm_metal_stream_h  *write_end
  )
{
  UINT32  r;
  UINT32  w;

  if (read_end == NULL || write_end == NULL) {
    return -1;
  }

  r = MetalStreamAlloc (PM_METAL_STREAM_KIND_PIPE);
  w = MetalStreamAlloc (PM_METAL_STREAM_KIND_PIPE);
  if (r == 0 || w == 0) {
    if (r != 0) {
      pm_metal_stream_close (r);
    }

    if (w != 0) {
      pm_metal_stream_close (w);
    }

    return -1;
  }

  if (MetalStreamRingAlloc (r) != 0) {
    pm_metal_stream_close (r);
    pm_metal_stream_close (w);
    return -1;
  }

  mSlots[r].peer = w;
  mSlots[w].peer = r;
  *read_end      = r;
  *write_end     = w;
  return 0;
}

int
pm_metal_stream_pty (
  pm_metal_stream_h  *master,
  pm_metal_stream_h  *slave
  )
{
  UINT32  m;
  UINT32  s;

  if (master == NULL || slave == NULL) {
    return -1;
  }

  m = MetalStreamAlloc (PM_METAL_STREAM_KIND_PTY);
  s = MetalStreamAlloc (PM_METAL_STREAM_KIND_PTY);
  if (m == 0 || s == 0) {
    if (m != 0) {
      pm_metal_stream_close (m);
    }

    if (s != 0) {
      pm_metal_stream_close (s);
    }

    return -1;
  }

  if (MetalStreamRingAlloc (m) != 0 || MetalStreamRingAlloc (s) != 0) {
    pm_metal_stream_close (m);
    pm_metal_stream_close (s);
    return -1;
  }

  mSlots[m].peer = s;
  mSlots[s].peer = m;
  *master        = m;
  *slave         = s;
  return 0;
}

void
pm_metal_stream_close (
  pm_metal_stream_h  h
  )
{
  UINT32  peer;

  if (h == 0 || h > PM_METAL_STREAM_MAX || !mSlots[h].used) {
    return;
  }

  peer = mSlots[h].peer;
  if (mSlots[h].ring != NULL) {
    pm_metal_mem_free (mSlots[h].ring);
  }

  ZeroMem (&mSlots[h], sizeof (mSlots[h]));
  if (peer != 0 && peer <= PM_METAL_STREAM_MAX && mSlots[peer].used) {
    mSlots[peer].peer = 0;
  }

  if (mStdIn == h) {
    mStdIn = 0;
  }

  if (mStdOut == h) {
    mStdOut = 0;
  }

  if (mStdErr == h) {
    mStdErr = 0;
  }
}

uint32_t
pm_metal_stream_write (
  pm_metal_stream_h  h,
  CONST VOID        *ptr,
  uint32_t           len
  )
{
  CONST UINT8  *p;
  CHAR8         line[256];
  UINT32        i;
  UINT32        o;

  if (h == 0 || h > PM_METAL_STREAM_MAX || !mSlots[h].used || ptr == NULL
      || len == 0)
  {
    return 0;
  }

  p = (CONST UINT8 *)ptr;

  if (mSlots[h].kind == PM_METAL_STREAM_KIND_UART) {
    /* Line-oriented serial; short-write not used. */
    o = 0;
    for (i = 0; i < len; i++) {
      if (p[i] == '\n' || o + 1 >= sizeof (line)) {
        line[o] = '\0';
        pm_metal_shell_serial_log (line);
        o = 0;
        if (p[i] == '\n') {
          continue;
        }
      }

      if (p[i] != '\r') {
        line[o++] = (CHAR8)p[i];
      }
    }

    if (o > 0) {
      line[o] = '\0';
      pm_metal_shell_serial_log (line);
    }

    return len;
  }

  if (mSlots[h].kind == PM_METAL_STREAM_KIND_UI_TAB) {
    o = 0;
    for (i = 0; i < len; i++) {
      if (p[i] == '\n' || o + 1 >= sizeof (line)) {
        line[o] = '\0';
        pm_metal_ui_tab_puts (mSlots[h].tab, line);
        o = 0;
        if (p[i] == '\n') {
          continue;
        }
      }

      if (p[i] != '\r') {
        line[o++] = (CHAR8)p[i];
      }
    }

    if (o > 0) {
      line[o] = '\0';
      pm_metal_ui_tab_puts (mSlots[h].tab, line);
    }

    return len;
  }

  if (mSlots[h].kind == PM_METAL_STREAM_KIND_PIPE
      || mSlots[h].kind == PM_METAL_STREAM_KIND_PTY)
  {
    UINT32  peer;

    peer = mSlots[h].peer;
    if (peer == 0) {
      return 0;
    }

    /* Write into peer's ring (reader drains peer). */
    return MetalStreamRingPut (peer, p, len);
  }

  return 0;
}

typedef struct {
  pm_metal_coro_t   coro;
  pm_metal_stream_h h;
  VOID             *ptr;
  UINT32            len;
  UINT32            n;
  INT32             is_drain;
  UINT64            deadline;
} pm_metal_stream_coro_t;

STATIC
pm_metal_status_t
MetalStreamReadFn (
  pm_metal_coro_t  *self
  )
{
  pm_metal_stream_coro_t  *s;
  pm_metal_stream_kind_t   kind;

  s = (pm_metal_stream_coro_t *)self;
  if (s->h == 0 || s->h > PM_METAL_STREAM_MAX || !mSlots[s->h].used) {
    return PM_METAL_ERROR;
  }

  kind = mSlots[s->h].kind;

  if (s->is_drain) {
    /* Await TX ring space on pipe/pty write end (peer ring). */
    if (kind == PM_METAL_STREAM_KIND_PIPE
        || kind == PM_METAL_STREAM_KIND_PTY)
    {
      UINT32  peer;

      peer = mSlots[s->h].peer;
      if (peer == 0) {
        return PM_METAL_ERROR;
      }

      if (MetalStreamRingSpace (peer) > 0) {
        self->result = (VOID *)(UINTN)0;
        return PM_METAL_DONE;
      }

      if (pm_metal_time_mono_us () > s->deadline) {
        return PM_METAL_ERROR;
      }

      return pm_metal_await (self, pm_metal_sleep_us (2000));
    }

    /* uart/ui_tab TX is unbounded line sink — drain completes. */
    self->result = (VOID *)(UINTN)0;
    return PM_METAL_DONE;
  }

  if (kind == PM_METAL_STREAM_KIND_PIPE
      || kind == PM_METAL_STREAM_KIND_PTY
      || kind == PM_METAL_STREAM_KIND_UART
      || kind == PM_METAL_STREAM_KIND_UI_TAB)
  {
    if (kind == PM_METAL_STREAM_KIND_UI_TAB
        && mSlots[s->h].ring == NULL)
    {
      if (MetalStreamRingAlloc (s->h) != 0) {
        return PM_METAL_ERROR;
      }
    }

    if (MetalStreamRingUsed (s->h) > 0) {
      s->n = MetalStreamRingGet (s->h, (UINT8 *)s->ptr, s->len);
      self->result = (VOID *)(UINTN)s->n;
      return PM_METAL_DONE;
    }

    if (pm_metal_time_mono_us () > s->deadline) {
      return PM_METAL_ERROR;
    }

    return pm_metal_await (self, pm_metal_sleep_us (2000));
  }

  return PM_METAL_ERROR;
}

pm_metal_async_handle_t
pm_metal_stream_read (
  pm_metal_stream_h  h,
  VOID              *ptr,
  uint32_t           len
  )
{
  pm_metal_stream_coro_t  *c;

  if (h == 0 || h > PM_METAL_STREAM_MAX || !mSlots[h].used) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  if (len > 0 && ptr == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  c = (pm_metal_stream_coro_t *)pm_metal_coro (
                                  MetalStreamReadFn,
                                  sizeof (*c)
                                  );
  if (c == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  c->h        = h;
  c->ptr      = ptr;
  c->len      = len;
  c->n        = 0;
  c->is_drain = 0;
  c->deadline = pm_metal_time_mono_us () + 30000000ull;
  return pm_metal_async_adopt_host_coro (&c->coro);
}

pm_metal_async_handle_t
pm_metal_stream_drain (
  pm_metal_stream_h  h
  )
{
  pm_metal_stream_coro_t  *c;

  if (h == 0 || h > PM_METAL_STREAM_MAX || !mSlots[h].used) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  c = (pm_metal_stream_coro_t *)pm_metal_coro (
                                  MetalStreamReadFn,
                                  sizeof (*c)
                                  );
  if (c == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  c->h        = h;
  c->ptr      = NULL;
  c->len      = 0;
  c->is_drain = 1;
  c->deadline = pm_metal_time_mono_us () + 30000000ull;
  return pm_metal_async_adopt_host_coro (&c->coro);
}

uint32_t
pm_metal_stream_feed_stdin (
  CONST VOID  *ptr,
  uint32_t     len
  )
{
  if (mStdIn == 0 || ptr == NULL || len == 0) {
    return 0;
  }

  if (mSlots[mStdIn].kind != PM_METAL_STREAM_KIND_UART
      && mSlots[mStdIn].kind != PM_METAL_STREAM_KIND_UI_TAB
      && mSlots[mStdIn].kind != PM_METAL_STREAM_KIND_PIPE
      && mSlots[mStdIn].kind != PM_METAL_STREAM_KIND_PTY)
  {
    return 0;
  }

  return MetalStreamRingPut (mStdIn, (CONST UINT8 *)ptr, len);
}

int
pm_metal_stdio_attach (
  pm_metal_stream_h  in,
  pm_metal_stream_h  out,
  pm_metal_stream_h  err
  )
{
  mStdIn  = in;
  mStdOut = out;
  mStdErr = err;
  return 0;
}

pm_metal_stream_h
pm_metal_stdio_in (
  VOID
  )
{
  return mStdIn;
}

pm_metal_stream_h
pm_metal_stdio_out (
  VOID
  )
{
  return mStdOut;
}

pm_metal_stream_h
pm_metal_stdio_err (
  VOID
  )
{
  return mStdErr;
}

uint32_t
pm_metal_stream_write_line (
  pm_metal_stream_h  h,
  CONST CHAR8       *line
  )
{
  CHAR8   buf[256];
  UINTN   n;
  UINTN   i;

  if (h == 0 || line == NULL) {
    return 0;
  }

  n = AsciiStrLen (line);
  if (n + 1 >= sizeof (buf)) {
    n = sizeof (buf) - 2;
  }

  for (i = 0; i < n; i++) {
    buf[i] = line[i];
  }

  buf[n]     = '\n';
  buf[n + 1] = '\0';
  return pm_metal_stream_write (h, buf, (UINT32)(n + 1));
}

STATIC UINT32
pm_metal_stream_open_uart_native (
  wasm_exec_env_t  exec_env
  )
{
  (VOID)exec_env;
  return pm_metal_stream_open_uart ();
}

STATIC UINT32
pm_metal_stream_open_ui_tab_native (
  wasm_exec_env_t  exec_env,
  UINT32           tab
  )
{
  (VOID)exec_env;
  return pm_metal_stream_open_ui_tab (tab);
}

STATIC INT32
pm_metal_stream_pipe_native (
  wasm_exec_env_t  exec_env,
  UINT32           read_out,
  UINT32           write_out
  )
{
  pm_metal_stream_h  r;
  pm_metal_stream_h  w;
  UINT32            *rn;
  UINT32            *wn;

  (VOID)exec_env;
  if (mStreamInst == NULL
      || !wasm_runtime_validate_app_addr (mStreamInst, read_out, 4)
      || !wasm_runtime_validate_app_addr (mStreamInst, write_out, 4))
  {
    return -1;
  }

  if (pm_metal_stream_pipe (&r, &w) != 0) {
    return -1;
  }

  rn  = (UINT32 *)wasm_runtime_addr_app_to_native (mStreamInst, read_out);
  wn  = (UINT32 *)wasm_runtime_addr_app_to_native (mStreamInst, write_out);
  *rn = r;
  *wn = w;
  return 0;
}

STATIC INT32
pm_metal_stream_pty_native (
  wasm_exec_env_t  exec_env,
  UINT32           master_out,
  UINT32           slave_out
  )
{
  pm_metal_stream_h  m;
  pm_metal_stream_h  s;
  UINT32            *mn;
  UINT32            *sn;

  (VOID)exec_env;
  if (mStreamInst == NULL
      || !wasm_runtime_validate_app_addr (mStreamInst, master_out, 4)
      || !wasm_runtime_validate_app_addr (mStreamInst, slave_out, 4))
  {
    return -1;
  }

  if (pm_metal_stream_pty (&m, &s) != 0) {
    return -1;
  }

  mn  = (UINT32 *)wasm_runtime_addr_app_to_native (mStreamInst, master_out);
  sn  = (UINT32 *)wasm_runtime_addr_app_to_native (mStreamInst, slave_out);
  *mn = m;
  *sn = s;
  return 0;
}

STATIC UINT32
pm_metal_stream_write_native (
  wasm_exec_env_t  exec_env,
  UINT32           h,
  UINT32           ptr,
  UINT32           len
  )
{
  VOID  *native;

  (VOID)exec_env;
  if (mStreamInst == NULL || len == 0) {
    return 0;
  }

  if (!wasm_runtime_validate_app_addr (mStreamInst, ptr, len)) {
    return 0;
  }

  native = wasm_runtime_addr_app_to_native (mStreamInst, ptr);
  return pm_metal_stream_write (h, native, len);
}

STATIC UINT32
pm_metal_stream_read_native (
  wasm_exec_env_t  exec_env,
  UINT32           h,
  UINT32           ptr,
  UINT32           len
  )
{
  VOID  *native;

  (VOID)exec_env;
  if (mStreamInst == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  if (len > 0 && !wasm_runtime_validate_app_addr (mStreamInst, ptr, len)) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  native = (len > 0) ? wasm_runtime_addr_app_to_native (mStreamInst, ptr) : NULL;
  return pm_metal_stream_read (h, native, len);
}

STATIC UINT32
pm_metal_stream_drain_native (
  wasm_exec_env_t  exec_env,
  UINT32           h
  )
{
  (VOID)exec_env;
  return pm_metal_stream_drain (h);
}

STATIC VOID
pm_metal_stream_close_native (
  wasm_exec_env_t  exec_env,
  UINT32           h
  )
{
  (VOID)exec_env;
  pm_metal_stream_close (h);
}

STATIC INT32
pm_metal_stdio_attach_native (
  wasm_exec_env_t  exec_env,
  UINT32           in,
  UINT32           out,
  UINT32           err
  )
{
  (VOID)exec_env;
  return pm_metal_stdio_attach (in, out, err);
}

STATIC NativeSymbol g_pm_metal_stream_native_symbols[] = {
  { "pm_metal_stream_open_uart", (VOID *)pm_metal_stream_open_uart_native, "()i", NULL },
  { "pm_metal_stream_open_ui_tab", (VOID *)pm_metal_stream_open_ui_tab_native, "(i)i", NULL },
  { "pm_metal_stream_pipe", (VOID *)pm_metal_stream_pipe_native, "(ii)i", NULL },
  { "pm_metal_stream_pty", (VOID *)pm_metal_stream_pty_native, "(ii)i", NULL },
  { "pm_metal_stream_write", (VOID *)pm_metal_stream_write_native, "(iii)i", NULL },
  { "pm_metal_stream_read", (VOID *)pm_metal_stream_read_native, "(iii)i", NULL },
  { "pm_metal_stream_drain", (VOID *)pm_metal_stream_drain_native, "(i)i", NULL },
  { "pm_metal_stream_close", (VOID *)pm_metal_stream_close_native, "(i)", NULL },
  { "pm_metal_stdio_attach", (VOID *)pm_metal_stdio_attach_native, "(iii)i", NULL },
};

int
pm_metal_stream_native_register (
  VOID
  )
{
  if (!wasm_runtime_register_natives (
         PM_METAL_STREAM_WASI_MODULE,
         g_pm_metal_stream_native_symbols,
         sizeof (g_pm_metal_stream_native_symbols)
           / sizeof (g_pm_metal_stream_native_symbols[0])
         ))
  {
    return -1;
  }

  return 0;
}
