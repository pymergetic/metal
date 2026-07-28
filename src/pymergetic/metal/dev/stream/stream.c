/** @file
  Metal byte streams — ui_tab / uart / pipe / pty. (impl: efi|bios)
**/
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/dev/stream/stream.h>
#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <pymergetic/metal/runtime/mem/mem.h>
#include <runtime/slot/slot_table.h>
#include <runtime/time/time.h>

#include "wasm_export.h"

#ifndef PM_METAL_STREAM_MAX
#define PM_METAL_STREAM_MAX 32u
#endif

#ifndef PM_METAL_STREAM_RING
#define PM_METAL_STREAM_RING 4096u
#endif

typedef enum {
  PM_METAL_STREAM_KIND_NONE = 0,
  PM_METAL_STREAM_KIND_UART,
  PM_METAL_STREAM_KIND_UI_TAB,
  PM_METAL_STREAM_KIND_PIPE,
  PM_METAL_STREAM_KIND_PTY
} pm_metal_stream_kind_t;

typedef struct {
  volatile uint32_t         used; /* slot ticket - see slot_table.h; must stay first */
  pm_metal_stream_kind_t    kind;
  pm_metal_ui_handle_t      tab;
  uint32_t                  peer; /* pipe/pty other end */
  uint8_t                  *ring;
  uint32_t                  rhead;
  uint32_t                  rtail;
  uint32_t                  rcap;
  pm_metal_stream_termios_t tio; /* PTY only; mirrored on peer */
  pm_metal_stream_winsize_t winsz;
} pm_metal_stream_slot_t;

static pm_metal_stream_slot_t mSlots[PM_METAL_STREAM_MAX + 1];
static pm_metal_stream_h      mStdIn;
static pm_metal_stream_h      mStdOut;
static pm_metal_stream_h      mStdErr;
static wasm_module_inst_t     mStreamInst;

void pm_metal_stream_bind_inst(void *module_inst)
{
  mStreamInst = (wasm_module_inst_t)module_inst;
}

static uint32_t MetalStreamAlloc(pm_metal_stream_kind_t kind)
{
  uint32_t i;

  /*
   * Tasks/fibers can run on any CPU now (no session pinning), so two
   * CPUs opening a stream at once must not be able to win the same
   * free index - claim the slot ticket with a CAS before touching it.
   */
  for (i = 1; i <= PM_METAL_STREAM_MAX; i++) {
    if (pm_metal_slot_try_claim(&mSlots[i].used, 1)) {
      pm_metal_slot_claimed_zero(&mSlots[i].used, sizeof(mSlots[i]));
      mSlots[i].kind = kind;
      return i;
    }
  }

  return 0;
}

static int32_t MetalStreamRingAlloc(uint32_t h)
{
  if (h == 0 || h > PM_METAL_STREAM_MAX) {
    return -1;
  }

  if (mSlots[h].ring != NULL) {
    return 0;
  }

  mSlots[h].ring =
    (uint8_t *)pm_metal_mem_alloc(PM_METAL_STREAM_RING, PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_NONE);
  if (mSlots[h].ring == NULL) {
    return -1;
  }

  mSlots[h].rcap  = PM_METAL_STREAM_RING;
  mSlots[h].rhead = 0;
  mSlots[h].rtail = 0;
  return 0;
}

static uint32_t MetalStreamRingUsed(uint32_t h)
{
  pm_metal_stream_slot_t *s;

  s = &mSlots[h];
  if (s->rhead >= s->rtail) {
    return s->rhead - s->rtail;
  }

  return s->rcap - (s->rtail - s->rhead);
}

static uint32_t MetalStreamRingSpace(uint32_t h)
{
  return mSlots[h].rcap - MetalStreamRingUsed(h) - 1u;
}

static uint32_t MetalStreamRingPut(uint32_t h, const uint8_t *data, uint32_t len)
{
  uint32_t n;
  uint32_t i;

  if (h == 0 || data == NULL || len == 0 || MetalStreamRingAlloc(h) != 0) {
    return 0;
  }

  n = MetalStreamRingSpace(h);
  if (n > len) {
    n = len;
  }

  for (i = 0; i < n; i++) {
    mSlots[h].ring[mSlots[h].rhead] = data[i];
    mSlots[h].rhead                 = (mSlots[h].rhead + 1u) % mSlots[h].rcap;
  }

  return n;
}

static uint32_t MetalStreamRingGet(uint32_t h, uint8_t *data, uint32_t len)
{
  uint32_t n;
  uint32_t i;
  uint32_t used;

  if (h == 0 || data == NULL || len == 0 || mSlots[h].ring == NULL) {
    return 0;
  }

  used = MetalStreamRingUsed(h);
  n    = used < len ? used : len;
  for (i = 0; i < n; i++) {
    data[i]         = mSlots[h].ring[mSlots[h].rtail];
    mSlots[h].rtail = (mSlots[h].rtail + 1u) % mSlots[h].rcap;
  }

  return n;
}

static void MetalStreamPtyAttrsInit(uint32_t h)
{
  pm_metal_stream_termios_t *t;
  pm_metal_stream_winsize_t *w;

  t = &mSlots[h].tio;
  w = &mSlots[h].winsz;
  memset(t, 0, sizeof(*t));
  memset(w, 0, sizeof(*w));
  t->iflag = PM_METAL_STREAM_ICRNL | PM_METAL_STREAM_IXON;
  t->oflag = PM_METAL_STREAM_OPOST | PM_METAL_STREAM_ONLCR;
  t->cflag = PM_METAL_STREAM_CS8 | PM_METAL_STREAM_CREAD | PM_METAL_STREAM_HUPCL;
  t->lflag = PM_METAL_STREAM_ISIG | PM_METAL_STREAM_ICANON | PM_METAL_STREAM_ECHO |
             PM_METAL_STREAM_ECHOE | PM_METAL_STREAM_ECHOK | PM_METAL_STREAM_IEXTEN;
  t->cc[PM_METAL_STREAM_VINTR]  = 3u;   /* Ctrl-C */
  t->cc[PM_METAL_STREAM_VQUIT]  = 28u;  /* Ctrl-\ */
  t->cc[PM_METAL_STREAM_VERASE] = 127u; /* DEL */
  t->cc[PM_METAL_STREAM_VKILL]  = 21u;  /* Ctrl-U */
  t->cc[PM_METAL_STREAM_VEOF]   = 4u;   /* Ctrl-D */
  t->cc[PM_METAL_STREAM_VTIME]  = 0u;
  t->cc[PM_METAL_STREAM_VMIN]   = 1u;
  t->cc[PM_METAL_STREAM_VSUSP]  = 26u; /* Ctrl-Z */
  w->row                        = 24u;
  w->col                        = 80u;
}

static int MetalStreamIsPty(uint32_t h)
{
  return (h != 0u && h <= PM_METAL_STREAM_MAX && mSlots[h].used &&
          mSlots[h].kind == PM_METAL_STREAM_KIND_PTY)
           ? 1
           : 0;
}

static void MetalStreamPtySyncPeer(uint32_t h)
{
  uint32_t peer;

  peer = mSlots[h].peer;
  if (!MetalStreamIsPty(peer)) {
    return;
  }

  mSlots[peer].tio   = mSlots[h].tio;
  mSlots[peer].winsz = mSlots[h].winsz;
}


pm_metal_stream_h pm_metal_stream_open_uart(void)
{
  uint32_t h;

  h = MetalStreamAlloc(PM_METAL_STREAM_KIND_UART);
  if (h == 0) {
    return PM_METAL_STREAM_INVALID;
  }

  if (MetalStreamRingAlloc(h) != 0) {
    pm_metal_stream_close(h);
    return PM_METAL_STREAM_INVALID;
  }

  return (pm_metal_stream_h)h;
}

pm_metal_stream_h pm_metal_stream_open_ui_tab(pm_metal_ui_handle_t tab)
{
  uint32_t h;

  if (tab == PM_METAL_UI_HANDLE_INVALID) {
    return PM_METAL_STREAM_INVALID;
  }

  h = MetalStreamAlloc(PM_METAL_STREAM_KIND_UI_TAB);
  if (h == 0) {
    return PM_METAL_STREAM_INVALID;
  }

  mSlots[h].tab = tab;
  return (pm_metal_stream_h)h;
}

int pm_metal_stream_pipe(pm_metal_stream_h *read_end, pm_metal_stream_h *write_end)
{
  uint32_t r;
  uint32_t w;

  if (read_end == NULL || write_end == NULL) {
    return -1;
  }

  r = MetalStreamAlloc(PM_METAL_STREAM_KIND_PIPE);
  w = MetalStreamAlloc(PM_METAL_STREAM_KIND_PIPE);
  if (r == 0 || w == 0) {
    if (r != 0) {
      pm_metal_stream_close(r);
    }

    if (w != 0) {
      pm_metal_stream_close(w);
    }

    return -1;
  }

  if (MetalStreamRingAlloc(r) != 0) {
    pm_metal_stream_close(r);
    pm_metal_stream_close(w);
    return -1;
  }

  mSlots[r].peer = w;
  mSlots[w].peer = r;
  *read_end      = r;
  *write_end     = w;
  return 0;
}

uint32_t pm_metal_stream_try_read(pm_metal_stream_h h, void *ptr, uint32_t len)
{
  if (h == 0 || h > PM_METAL_STREAM_MAX || !mSlots[h].used || ptr == NULL || len == 0) {
    return 0;
  }

  if (mSlots[h].kind != PM_METAL_STREAM_KIND_PIPE && mSlots[h].kind != PM_METAL_STREAM_KIND_PTY) {
    return 0;
  }

  if (MetalStreamRingUsed(h) == 0) {
    return 0;
  }

  return MetalStreamRingGet(h, (uint8_t *)ptr, len);
}

int pm_metal_stream_pty(pm_metal_stream_h *master, pm_metal_stream_h *slave)
{
  uint32_t m;
  uint32_t s;

  if (master == NULL || slave == NULL) {
    return -1;
  }

  m = MetalStreamAlloc(PM_METAL_STREAM_KIND_PTY);
  s = MetalStreamAlloc(PM_METAL_STREAM_KIND_PTY);
  if (m == 0 || s == 0) {
    if (m != 0) {
      pm_metal_stream_close(m);
    }

    if (s != 0) {
      pm_metal_stream_close(s);
    }

    return -1;
  }

  if (MetalStreamRingAlloc(m) != 0 || MetalStreamRingAlloc(s) != 0) {
    pm_metal_stream_close(m);
    pm_metal_stream_close(s);
    return -1;
  }

  mSlots[m].peer = s;
  mSlots[s].peer = m;
  MetalStreamPtyAttrsInit(m);
  MetalStreamPtyAttrsInit(s);
  *master = m;
  *slave  = s;
  return 0;
}

int pm_metal_stream_termios_get(pm_metal_stream_h h, pm_metal_stream_termios_t *out)
{
  if (!MetalStreamIsPty(h) || out == NULL) {
    return -1;
  }

  *out = mSlots[h].tio;
  return 0;
}

int pm_metal_stream_termios_set(pm_metal_stream_h h, const pm_metal_stream_termios_t *in)
{
  if (!MetalStreamIsPty(h) || in == NULL) {
    return -1;
  }

  mSlots[h].tio = *in;
  MetalStreamPtySyncPeer(h);
  return 0;
}

int pm_metal_stream_winsize_get(pm_metal_stream_h h, pm_metal_stream_winsize_t *out)
{
  if (!MetalStreamIsPty(h) || out == NULL) {
    return -1;
  }

  *out = mSlots[h].winsz;
  return 0;
}

int pm_metal_stream_winsize_set(pm_metal_stream_h h, const pm_metal_stream_winsize_t *in)
{
  if (!MetalStreamIsPty(h) || in == NULL) {
    return -1;
  }

  mSlots[h].winsz = *in;
  MetalStreamPtySyncPeer(h);
  return 0;
}

uint32_t pm_metal_stream_pending(pm_metal_stream_h h)
{
  if (h == 0 || h > PM_METAL_STREAM_MAX || !mSlots[h].used) {
    return 0;
  }

  if (mSlots[h].kind != PM_METAL_STREAM_KIND_PIPE && mSlots[h].kind != PM_METAL_STREAM_KIND_PTY &&
      mSlots[h].kind != PM_METAL_STREAM_KIND_UART &&
      mSlots[h].kind != PM_METAL_STREAM_KIND_UI_TAB) {
    return 0;
  }

  return MetalStreamRingUsed(h);
}

void pm_metal_stream_close(pm_metal_stream_h h)
{
  uint32_t peer;

  if (h == 0 || h > PM_METAL_STREAM_MAX || !mSlots[h].used) {
    return;
  }

  peer = mSlots[h].peer;
  if (mSlots[h].ring != NULL) {
    pm_metal_mem_free(mSlots[h].ring);
  }

  memset(&mSlots[h], 0, sizeof(mSlots[h]));
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

uint32_t pm_metal_stream_write(pm_metal_stream_h h, const void *ptr, uint32_t len)
{
  const uint8_t *p;
  char           line[256];
  uint32_t       i;
  uint32_t       o;

  if (h == 0 || h > PM_METAL_STREAM_MAX || !mSlots[h].used || ptr == NULL || len == 0) {
    return 0;
  }

  p = (const uint8_t *)ptr;

  if (mSlots[h].kind == PM_METAL_STREAM_KIND_UART) {
    /* Line-oriented serial; short-write not used. */
    o = 0;
    for (i = 0; i < len; i++) {
      if (p[i] == '\n' || o + 1 >= sizeof(line)) {
        line[o] = '\0';
        pm_metal_shell_serial_log(line);
        o = 0;
        if (p[i] == '\n') {
          continue;
        }
      }

      if (p[i] != '\r') {
        line[o++] = (char)p[i];
      }
    }

    if (o > 0) {
      line[o] = '\0';
      pm_metal_shell_serial_log(line);
    }

    return len;
  }

  if (mSlots[h].kind == PM_METAL_STREAM_KIND_UI_TAB) {
    o = 0;
    for (i = 0; i < len; i++) {
      if (p[i] == '\n' || o + 1 >= sizeof(line)) {
        line[o] = '\0';
        pm_metal_ui_tab_puts(mSlots[h].tab, line);
        o = 0;
        if (p[i] == '\n') {
          continue;
        }
      }

      if (p[i] != '\r') {
        line[o++] = (char)p[i];
      }
    }

    if (o > 0) {
      line[o] = '\0';
      pm_metal_ui_tab_puts(mSlots[h].tab, line);
    }

    return len;
  }

  if (mSlots[h].kind == PM_METAL_STREAM_KIND_PIPE || mSlots[h].kind == PM_METAL_STREAM_KIND_PTY) {
    uint32_t peer;

    peer = mSlots[h].peer;
    if (peer == 0) {
      return 0;
    }

    /* Write into peer's ring (reader drains peer). */
    return MetalStreamRingPut(peer, p, len);
  }

  return 0;
}

typedef struct {
  pm_metal_stream_h h;
  void             *ptr;
  uint32_t          len;
  uint32_t          n;
  int32_t           is_drain;
  uint64_t          deadline;
} pm_metal_stream_coro_t;

static pm_metal_status_t MetalStreamReadStep(pm_metal_async_handle_t self_h)
{
  pm_metal_stream_coro_t *s;
  pm_metal_stream_kind_t  kind;

  s = (pm_metal_stream_coro_t *)(uintptr_t)pm_metal_async_coro_state(self_h);
  if (s == NULL || s->h == 0 || s->h > PM_METAL_STREAM_MAX || !mSlots[s->h].used) {
    return PM_METAL_ERROR;
  }

  kind = mSlots[s->h].kind;

  if (s->is_drain) {
    /* Await TX ring space on pipe/pty write end (peer ring). */
    if (kind == PM_METAL_STREAM_KIND_PIPE || kind == PM_METAL_STREAM_KIND_PTY) {
      uint32_t peer;

      peer = mSlots[s->h].peer;
      if (peer == 0) {
        return PM_METAL_ERROR;
      }

      if (MetalStreamRingSpace(peer) > 0) {
        pm_metal_async_set_result_u32(self_h, 0);
        return PM_METAL_DONE;
      }

      if (pm_metal_time_mono_us() > s->deadline) {
        return PM_METAL_ERROR;
      }

      return pm_metal_async_await(self_h, pm_metal_async_sleep_us(2000));
    }

    /* uart/ui_tab TX is unbounded line sink — drain completes. */
    pm_metal_async_set_result_u32(self_h, 0);
    return PM_METAL_DONE;
  }

  if (kind == PM_METAL_STREAM_KIND_PIPE || kind == PM_METAL_STREAM_KIND_PTY ||
      kind == PM_METAL_STREAM_KIND_UART || kind == PM_METAL_STREAM_KIND_UI_TAB) {
    if (kind == PM_METAL_STREAM_KIND_UI_TAB && mSlots[s->h].ring == NULL) {
      if (MetalStreamRingAlloc(s->h) != 0) {
        return PM_METAL_ERROR;
      }
    }

    if (MetalStreamRingUsed(s->h) > 0) {
      s->n = MetalStreamRingGet(s->h, (uint8_t *)s->ptr, s->len);
      pm_metal_async_set_result_u32(self_h, s->n);
      return PM_METAL_DONE;
    }

    if (pm_metal_time_mono_us() > s->deadline) {
      return PM_METAL_ERROR;
    }

    return pm_metal_async_await(self_h, pm_metal_async_sleep_us(2000));
  }

  return PM_METAL_ERROR;
}

pm_metal_async_handle_t pm_metal_stream_read(pm_metal_stream_h h, void *ptr, uint32_t len)
{
  pm_metal_async_handle_t ah;
  pm_metal_stream_coro_t *c;

  if (h == 0 || h > PM_METAL_STREAM_MAX || !mSlots[h].used) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  if (len > 0 && ptr == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  ah = pm_metal_async_coro_create(MetalStreamReadStep, sizeof(*c));
  if (ah == PM_METAL_ASYNC_HANDLE_INVALID) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  c = (pm_metal_stream_coro_t *)(uintptr_t)pm_metal_async_coro_state(ah);
  if (c == NULL) {
    pm_metal_async_coro_close(ah);
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  c->h        = h;
  c->ptr      = ptr;
  c->len      = len;
  c->n        = 0;
  c->is_drain = 0;
  c->deadline = pm_metal_time_mono_us() + 30000000ull;
  return ah;
}

pm_metal_async_handle_t pm_metal_stream_drain(pm_metal_stream_h h)
{
  pm_metal_async_handle_t ah;
  pm_metal_stream_coro_t *c;

  if (h == 0 || h > PM_METAL_STREAM_MAX || !mSlots[h].used) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  ah = pm_metal_async_coro_create(MetalStreamReadStep, sizeof(*c));
  if (ah == PM_METAL_ASYNC_HANDLE_INVALID) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  c = (pm_metal_stream_coro_t *)(uintptr_t)pm_metal_async_coro_state(ah);
  if (c == NULL) {
    pm_metal_async_coro_close(ah);
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  c->h        = h;
  c->ptr      = NULL;
  c->len      = 0;
  c->is_drain = 1;
  c->deadline = pm_metal_time_mono_us() + 30000000ull;
  return ah;
}

uint32_t pm_metal_stream_feed_stdin(const void *ptr, uint32_t len)
{
  if (mStdIn == 0 || ptr == NULL || len == 0) {
    return 0;
  }

  if (mSlots[mStdIn].kind != PM_METAL_STREAM_KIND_UART &&
      mSlots[mStdIn].kind != PM_METAL_STREAM_KIND_UI_TAB &&
      mSlots[mStdIn].kind != PM_METAL_STREAM_KIND_PIPE &&
      mSlots[mStdIn].kind != PM_METAL_STREAM_KIND_PTY) {
    return 0;
  }

  return MetalStreamRingPut(mStdIn, (const uint8_t *)ptr, len);
}

int pm_metal_stdio_attach(pm_metal_stream_h in, pm_metal_stream_h out, pm_metal_stream_h err)
{
  mStdIn  = in;
  mStdOut = out;
  mStdErr = err;
  return 0;
}

pm_metal_stream_h pm_metal_stdio_in(void)
{
  return mStdIn;
}

pm_metal_stream_h pm_metal_stdio_out(void)
{
  return mStdOut;
}

pm_metal_stream_h pm_metal_stdio_err(void)
{
  return mStdErr;
}

uint32_t pm_metal_stream_write_line(pm_metal_stream_h h, const char *line)
{
  char      buf[256];
  uintptr_t n;
  uintptr_t i;

  if (h == 0 || line == NULL) {
    return 0;
  }

  n = strlen(line);
  if (n + 1 >= sizeof(buf)) {
    n = sizeof(buf) - 2;
  }

  for (i = 0; i < n; i++) {
    buf[i] = line[i];
  }

  buf[n]     = '\n';
  buf[n + 1] = '\0';
  return pm_metal_stream_write(h, buf, (uint32_t)(n + 1));
}

static uint32_t pm_metal_stream_open_uart_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_stream_open_uart();
}

static uint32_t pm_metal_stream_open_ui_tab_native(wasm_exec_env_t exec_env, uint32_t tab)
{
  (void)exec_env;
  return pm_metal_stream_open_ui_tab(tab);
}

static int32_t pm_metal_stream_pipe_native(wasm_exec_env_t exec_env,
                                           uint32_t        read_out,
                                           uint32_t        write_out)
{
  pm_metal_stream_h r;
  pm_metal_stream_h w;
  uint32_t         *rn;
  uint32_t         *wn;

  (void)exec_env;
  if (mStreamInst == NULL || !wasm_runtime_validate_app_addr(mStreamInst, read_out, 4) ||
      !wasm_runtime_validate_app_addr(mStreamInst, write_out, 4)) {
    return -1;
  }

  if (pm_metal_stream_pipe(&r, &w) != 0) {
    return -1;
  }

  rn  = (uint32_t *)wasm_runtime_addr_app_to_native(mStreamInst, read_out);
  wn  = (uint32_t *)wasm_runtime_addr_app_to_native(mStreamInst, write_out);
  *rn = r;
  *wn = w;
  return 0;
}

static int32_t pm_metal_stream_pty_native(wasm_exec_env_t exec_env,
                                          uint32_t        master_out,
                                          uint32_t        slave_out)
{
  pm_metal_stream_h m;
  pm_metal_stream_h s;
  uint32_t         *mn;
  uint32_t         *sn;

  (void)exec_env;
  if (mStreamInst == NULL || !wasm_runtime_validate_app_addr(mStreamInst, master_out, 4) ||
      !wasm_runtime_validate_app_addr(mStreamInst, slave_out, 4)) {
    return -1;
  }

  if (pm_metal_stream_pty(&m, &s) != 0) {
    return -1;
  }

  mn  = (uint32_t *)wasm_runtime_addr_app_to_native(mStreamInst, master_out);
  sn  = (uint32_t *)wasm_runtime_addr_app_to_native(mStreamInst, slave_out);
  *mn = m;
  *sn = s;
  return 0;
}

static uint32_t pm_metal_stream_write_native(wasm_exec_env_t exec_env,
                                             uint32_t        h,
                                             uint32_t        ptr,
                                             uint32_t        len)
{
  void *native;

  (void)exec_env;
  if (mStreamInst == NULL || len == 0) {
    return 0;
  }

  if (!wasm_runtime_validate_app_addr(mStreamInst, ptr, len)) {
    return 0;
  }

  native = wasm_runtime_addr_app_to_native(mStreamInst, ptr);
  return pm_metal_stream_write(h, native, len);
}

static uint32_t pm_metal_stream_read_native(wasm_exec_env_t exec_env,
                                            uint32_t        h,
                                            uint32_t        ptr,
                                            uint32_t        len)
{
  void *native;

  (void)exec_env;
  if (mStreamInst == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  if (len > 0 && !wasm_runtime_validate_app_addr(mStreamInst, ptr, len)) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  native = (len > 0) ? wasm_runtime_addr_app_to_native(mStreamInst, ptr) : NULL;
  return pm_metal_stream_read(h, native, len);
}

static uint32_t pm_metal_stream_drain_native(wasm_exec_env_t exec_env, uint32_t h)
{
  (void)exec_env;
  return pm_metal_stream_drain(h);
}

static void pm_metal_stream_close_native(wasm_exec_env_t exec_env, uint32_t h)
{
  (void)exec_env;
  pm_metal_stream_close(h);
}

static int32_t pm_metal_stdio_attach_native(wasm_exec_env_t exec_env,
                                            uint32_t        in,
                                            uint32_t        out,
                                            uint32_t        err)
{
  (void)exec_env;
  return pm_metal_stdio_attach(in, out, err);
}

static NativeSymbol g_pm_metal_stream_native_symbols[] = {
  { "pm_metal_stream_open_uart", (void *)pm_metal_stream_open_uart_native, "()i", NULL },
  { "pm_metal_stream_open_ui_tab", (void *)pm_metal_stream_open_ui_tab_native, "(i)i", NULL },
  { "pm_metal_stream_pipe", (void *)pm_metal_stream_pipe_native, "(ii)i", NULL },
  { "pm_metal_stream_pty", (void *)pm_metal_stream_pty_native, "(ii)i", NULL },
  { "pm_metal_stream_write", (void *)pm_metal_stream_write_native, "(iii)i", NULL },
  { "pm_metal_stream_read", (void *)pm_metal_stream_read_native, "(iii)i", NULL },
  { "pm_metal_stream_drain", (void *)pm_metal_stream_drain_native, "(i)i", NULL },
  { "pm_metal_stream_close", (void *)pm_metal_stream_close_native, "(i)", NULL },
  { "pm_metal_stdio_attach", (void *)pm_metal_stdio_attach_native, "(iii)i", NULL },
};

int pm_metal_stream_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_STREAM_WASI_MODULE,
                                     g_pm_metal_stream_native_symbols,
                                     sizeof(g_pm_metal_stream_native_symbols) /
                                       sizeof(g_pm_metal_stream_native_symbols[0]))) {
    return -1;
  }

  return 0;
}
