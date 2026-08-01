/*
 * Metal byte streams — pipe / pty rings + async read/drain.
 */
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/async/handle.h>
#include <pymergetic/metal/async/await.h>
#include <pymergetic/metal/async/coro.h>
#include <pymergetic/metal/async/time.h>
#include <pymergetic/metal/dev/stream/__init__.h>
#include <pymergetic/metal/mem/__init__.h>

#ifndef PM_METAL_STREAM_MAX
#define PM_METAL_STREAM_MAX 32u
#endif

#ifndef PM_METAL_STREAM_RING
#define PM_METAL_STREAM_RING 4096u
#endif

typedef enum {
  PM_METAL_STREAM_KIND_NONE = 0,
  PM_METAL_STREAM_KIND_PIPE,
  PM_METAL_STREAM_KIND_PTY
} pm_metal_stream_kind_t;

typedef struct {
  uint32_t used;
  pm_metal_stream_kind_t kind;
  uint32_t peer;
  uint8_t *ring;
  uint32_t rhead;
  uint32_t rtail;
  uint32_t rcap;
  pm_metal_stream_termios_t tio;
  pm_metal_stream_winsize_t winsz;
} pm_metal_stream_slot_t;

typedef struct {
  pm_metal_stream_h h;
  void *ptr;
  uint32_t len;
  uint32_t n;
  int32_t is_drain;
  uint64_t deadline;
} pm_metal_stream_coro_t;

static pm_metal_stream_slot_t g_slots[PM_METAL_STREAM_MAX + 1u];
static pm_metal_stream_h g_stdin;
static pm_metal_stream_h g_stdout;
static pm_metal_stream_h g_stderr;

static uint32_t stream_alloc(pm_metal_stream_kind_t kind)
{
  uint32_t i;

  for (i = 1u; i <= PM_METAL_STREAM_MAX; i++) {
    if (g_slots[i].used == 0u) {
      memset(&g_slots[i], 0, sizeof(g_slots[i]));
      g_slots[i].used = 1u;
      g_slots[i].kind = kind;
      return i;
    }
  }
  return 0u;
}

static int32_t ring_alloc(uint32_t h)
{
  if (h == 0u || h > PM_METAL_STREAM_MAX) {
    return -1;
  }
  if (g_slots[h].ring != NULL) {
    return 0;
  }
  g_slots[h].ring = pm_metal_mem_alloc(PM_METAL_STREAM_RING);
  if (g_slots[h].ring == NULL) {
    return -1;
  }
  g_slots[h].rcap = PM_METAL_STREAM_RING;
  g_slots[h].rhead = 0u;
  g_slots[h].rtail = 0u;
  return 0;
}

static uint32_t ring_used(uint32_t h)
{
  pm_metal_stream_slot_t *s;

  s = &g_slots[h];
  if (s->rhead >= s->rtail) {
    return s->rhead - s->rtail;
  }
  return s->rcap - (s->rtail - s->rhead);
}

static uint32_t ring_space(uint32_t h)
{
  return g_slots[h].rcap - ring_used(h) - 1u;
}

static uint32_t ring_put(uint32_t h, const uint8_t *data, uint32_t len)
{
  uint32_t n;
  uint32_t i;

  if (h == 0u || data == NULL || len == 0u || ring_alloc(h) != 0) {
    return 0u;
  }
  n = ring_space(h);
  if (n > len) {
    n = len;
  }
  for (i = 0u; i < n; i++) {
    g_slots[h].ring[g_slots[h].rhead] = data[i];
    g_slots[h].rhead = (g_slots[h].rhead + 1u) % g_slots[h].rcap;
  }
  return n;
}

static uint32_t ring_get(uint32_t h, uint8_t *data, uint32_t len)
{
  uint32_t n;
  uint32_t i;
  uint32_t used;

  if (h == 0u || data == NULL || len == 0u || g_slots[h].ring == NULL) {
    return 0u;
  }
  used = ring_used(h);
  n = used < len ? used : len;
  for (i = 0u; i < n; i++) {
    data[i] = g_slots[h].ring[g_slots[h].rtail];
    g_slots[h].rtail = (g_slots[h].rtail + 1u) % g_slots[h].rcap;
  }
  return n;
}

static void pty_attrs_init(uint32_t h)
{
  pm_metal_stream_termios_t *t;
  pm_metal_stream_winsize_t *w;

  t = &g_slots[h].tio;
  w = &g_slots[h].winsz;
  memset(t, 0, sizeof(*t));
  memset(w, 0, sizeof(*w));
  t->iflag = PM_METAL_STREAM_ICRNL | PM_METAL_STREAM_IXON;
  t->oflag = PM_METAL_STREAM_OPOST | PM_METAL_STREAM_ONLCR;
  t->cflag = PM_METAL_STREAM_CS8 | PM_METAL_STREAM_CREAD | PM_METAL_STREAM_HUPCL;
  t->lflag = PM_METAL_STREAM_ISIG | PM_METAL_STREAM_ICANON | PM_METAL_STREAM_ECHO |
             PM_METAL_STREAM_ECHOE | PM_METAL_STREAM_ECHOK | PM_METAL_STREAM_IEXTEN;
  t->cc[PM_METAL_STREAM_VINTR] = 3u;
  t->cc[PM_METAL_STREAM_VQUIT] = 28u;
  t->cc[PM_METAL_STREAM_VERASE] = 127u;
  t->cc[PM_METAL_STREAM_VKILL] = 21u;
  t->cc[PM_METAL_STREAM_VEOF] = 4u;
  t->cc[PM_METAL_STREAM_VTIME] = 0u;
  t->cc[PM_METAL_STREAM_VMIN] = 1u;
  t->cc[PM_METAL_STREAM_VSUSP] = 26u;
  w->row = 24u;
  w->col = 80u;
}

static int32_t is_pty(uint32_t h)
{
  return (h != 0u && h <= PM_METAL_STREAM_MAX && g_slots[h].used != 0u &&
          g_slots[h].kind == PM_METAL_STREAM_KIND_PTY)
           ? 1
           : 0;
}

static void pty_sync_peer(uint32_t h)
{
  uint32_t peer;

  peer = g_slots[h].peer;
  if (is_pty(peer) == 0) {
    return;
  }
  g_slots[peer].tio = g_slots[h].tio;
  g_slots[peer].winsz = g_slots[h].winsz;
}

int32_t pm_metal_stream_pipe(pm_metal_stream_h *read_end, pm_metal_stream_h *write_end)
{
  uint32_t r;
  uint32_t w;

  if (read_end == NULL || write_end == NULL) {
    return -1;
  }
  r = stream_alloc(PM_METAL_STREAM_KIND_PIPE);
  w = stream_alloc(PM_METAL_STREAM_KIND_PIPE);
  if (r == 0u || w == 0u) {
    if (r != 0u) {
      pm_metal_stream_close(r);
    }
    if (w != 0u) {
      pm_metal_stream_close(w);
    }
    return -1;
  }
  if (ring_alloc(r) != 0) {
    pm_metal_stream_close(r);
    pm_metal_stream_close(w);
    return -1;
  }
  g_slots[r].peer = w;
  g_slots[w].peer = r;
  *read_end = r;
  *write_end = w;
  return 0;
}

int32_t pm_metal_stream_pty(pm_metal_stream_h *master, pm_metal_stream_h *slave)
{
  uint32_t m;
  uint32_t s;

  if (master == NULL || slave == NULL) {
    return -1;
  }
  m = stream_alloc(PM_METAL_STREAM_KIND_PTY);
  s = stream_alloc(PM_METAL_STREAM_KIND_PTY);
  if (m == 0u || s == 0u) {
    if (m != 0u) {
      pm_metal_stream_close(m);
    }
    if (s != 0u) {
      pm_metal_stream_close(s);
    }
    return -1;
  }
  if (ring_alloc(m) != 0 || ring_alloc(s) != 0) {
    pm_metal_stream_close(m);
    pm_metal_stream_close(s);
    return -1;
  }
  g_slots[m].peer = s;
  g_slots[s].peer = m;
  pty_attrs_init(m);
  pty_attrs_init(s);
  *master = m;
  *slave = s;
  return 0;
}

uint32_t pm_metal_stream_try_read(pm_metal_stream_h h, void *ptr, uint32_t len)
{
  if (h == 0u || h > PM_METAL_STREAM_MAX || g_slots[h].used == 0u || ptr == NULL || len == 0u) {
    return 0u;
  }
  if (g_slots[h].kind != PM_METAL_STREAM_KIND_PIPE && g_slots[h].kind != PM_METAL_STREAM_KIND_PTY) {
    return 0u;
  }
  if (ring_used(h) == 0u) {
    return 0u;
  }
  return ring_get(h, (uint8_t *)ptr, len);
}

int32_t pm_metal_stream_termios_get(pm_metal_stream_h h, pm_metal_stream_termios_t *out)
{
  if (is_pty(h) == 0 || out == NULL) {
    return -1;
  }
  *out = g_slots[h].tio;
  return 0;
}

int32_t pm_metal_stream_termios_set(pm_metal_stream_h h, const pm_metal_stream_termios_t *in)
{
  if (is_pty(h) == 0 || in == NULL) {
    return -1;
  }
  g_slots[h].tio = *in;
  pty_sync_peer(h);
  return 0;
}

int32_t pm_metal_stream_winsize_get(pm_metal_stream_h h, pm_metal_stream_winsize_t *out)
{
  if (is_pty(h) == 0 || out == NULL) {
    return -1;
  }
  *out = g_slots[h].winsz;
  return 0;
}

int32_t pm_metal_stream_winsize_set(pm_metal_stream_h h, const pm_metal_stream_winsize_t *in)
{
  if (is_pty(h) == 0 || in == NULL) {
    return -1;
  }
  g_slots[h].winsz = *in;
  pty_sync_peer(h);
  return 0;
}

uint32_t pm_metal_stream_pending(pm_metal_stream_h h)
{
  if (h == 0u || h > PM_METAL_STREAM_MAX || g_slots[h].used == 0u) {
    return 0u;
  }
  if (g_slots[h].kind != PM_METAL_STREAM_KIND_PIPE && g_slots[h].kind != PM_METAL_STREAM_KIND_PTY) {
    return 0u;
  }
  return ring_used(h);
}

void pm_metal_stream_close(pm_metal_stream_h h)
{
  uint32_t peer;

  if (h == 0u || h > PM_METAL_STREAM_MAX || g_slots[h].used == 0u) {
    return;
  }
  peer = g_slots[h].peer;
  if (g_slots[h].ring != NULL) {
    pm_metal_mem_free(g_slots[h].ring);
  }
  memset(&g_slots[h], 0, sizeof(g_slots[h]));
  if (peer != 0u && peer <= PM_METAL_STREAM_MAX && g_slots[peer].used != 0u) {
    g_slots[peer].peer = 0u;
  }
  if (g_stdin == h) {
    g_stdin = 0u;
  }
  if (g_stdout == h) {
    g_stdout = 0u;
  }
  if (g_stderr == h) {
    g_stderr = 0u;
  }
}

uint32_t pm_metal_stream_write(pm_metal_stream_h h, const void *ptr, uint32_t len)
{
  const uint8_t *p;
  uint32_t peer;

  if (h == 0u || h > PM_METAL_STREAM_MAX || g_slots[h].used == 0u || ptr == NULL || len == 0u) {
    return 0u;
  }
  if (g_slots[h].kind != PM_METAL_STREAM_KIND_PIPE && g_slots[h].kind != PM_METAL_STREAM_KIND_PTY) {
    return 0u;
  }
  peer = g_slots[h].peer;
  if (peer == 0u) {
    return 0u;
  }
  p = (const uint8_t *)ptr;
  return ring_put(peer, p, len);
}

static uint32_t stream_read_step(uint32_t self_h)
{
  pm_metal_stream_coro_t *s;
  pm_metal_stream_kind_t kind;

  s = (pm_metal_stream_coro_t *)pm_metal_async_coro_state(self_h);
  if (s == NULL || s->h == 0u || s->h > PM_METAL_STREAM_MAX || g_slots[s->h].used == 0u) {
    return (uint32_t)PM_METAL_ASYNC_ERROR;
  }
  kind = g_slots[s->h].kind;
  if (s->is_drain != 0) {
    uint32_t peer;

    if (kind != PM_METAL_STREAM_KIND_PIPE && kind != PM_METAL_STREAM_KIND_PTY) {
      return (uint32_t)PM_METAL_ASYNC_ERROR;
    }
    peer = g_slots[s->h].peer;
    if (peer == 0u) {
      return (uint32_t)PM_METAL_ASYNC_ERROR;
    }
    if (ring_space(peer) > 0u) {
      pm_metal_async_set_result_u32(self_h, 0u);
      return (uint32_t)PM_METAL_ASYNC_DONE;
    }
    if (pm_metal_time_mono_us() > s->deadline) {
      return (uint32_t)PM_METAL_ASYNC_ERROR;
    }
    return (uint32_t)pm_metal_async_await(self_h, pm_metal_async_sleep_us(2000ull));
  }
  if (kind != PM_METAL_STREAM_KIND_PIPE && kind != PM_METAL_STREAM_KIND_PTY) {
    return (uint32_t)PM_METAL_ASYNC_ERROR;
  }
  if (ring_used(s->h) > 0u) {
    s->n = ring_get(s->h, (uint8_t *)s->ptr, s->len);
    pm_metal_async_set_result_u32(self_h, s->n);
    return (uint32_t)PM_METAL_ASYNC_DONE;
  }
  if (pm_metal_time_mono_us() > s->deadline) {
    return (uint32_t)PM_METAL_ASYNC_ERROR;
  }
  return (uint32_t)pm_metal_async_await(self_h, pm_metal_async_sleep_us(2000ull));
}

uint32_t pm_metal_stream_read(pm_metal_stream_h h, void *ptr, uint32_t len)
{
  uint32_t ah;
  pm_metal_stream_coro_t *c;

  if (h == 0u || h > PM_METAL_STREAM_MAX || g_slots[h].used == 0u) {
    return 0u;
  }
  if (len > 0u && ptr == NULL) {
    return 0u;
  }
  ah = pm_metal_async_coro_create(stream_read_step, (uint32_t)sizeof(*c));
  if (ah == 0u) {
    return 0u;
  }
  c = (pm_metal_stream_coro_t *)pm_metal_async_coro_state(ah);
  if (c == NULL) {
    pm_metal_async_coro_close(ah);
    return 0u;
  }
  c->h = h;
  c->ptr = ptr;
  c->len = len;
  c->n = 0u;
  c->is_drain = 0;
  c->deadline = pm_metal_time_mono_us() + 30000000ull;
  return ah;
}

uint32_t pm_metal_stream_drain(pm_metal_stream_h h)
{
  uint32_t ah;
  pm_metal_stream_coro_t *c;

  if (h == 0u || h > PM_METAL_STREAM_MAX || g_slots[h].used == 0u) {
    return 0u;
  }
  ah = pm_metal_async_coro_create(stream_read_step, (uint32_t)sizeof(*c));
  if (ah == 0u) {
    return 0u;
  }
  c = (pm_metal_stream_coro_t *)pm_metal_async_coro_state(ah);
  if (c == NULL) {
    pm_metal_async_coro_close(ah);
    return 0u;
  }
  c->h = h;
  c->ptr = NULL;
  c->len = 0u;
  c->is_drain = 1;
  c->deadline = pm_metal_time_mono_us() + 30000000ull;
  return ah;
}

uint32_t pm_metal_stream_feed_stdin(const void *ptr, uint32_t len)
{
  if (g_stdin == 0u || ptr == NULL || len == 0u) {
    return 0u;
  }
  if (g_slots[g_stdin].kind != PM_METAL_STREAM_KIND_PIPE &&
      g_slots[g_stdin].kind != PM_METAL_STREAM_KIND_PTY) {
    return 0u;
  }
  return ring_put(g_stdin, (const uint8_t *)ptr, len);
}

int32_t pm_metal_stdio_attach(pm_metal_stream_h in, pm_metal_stream_h out, pm_metal_stream_h err)
{
  g_stdin = in;
  g_stdout = out;
  g_stderr = err;
  return 0;
}

pm_metal_stream_h pm_metal_stdio_in(void)
{
  return g_stdin;
}

pm_metal_stream_h pm_metal_stdio_out(void)
{
  return g_stdout;
}

pm_metal_stream_h pm_metal_stdio_err(void)
{
  return g_stderr;
}

uint32_t pm_metal_stream_write_line(pm_metal_stream_h h, const char *line)
{
  char buf[256];
  size_t n;
  size_t i;

  if (h == 0u || line == NULL) {
    return 0u;
  }
  n = strlen(line);
  if (n + 1u >= sizeof(buf)) {
    n = sizeof(buf) - 2u;
  }
  for (i = 0u; i < n; i++) {
    buf[i] = line[i];
  }
  buf[n] = '\n';
  buf[n + 1u] = '\0';
  return pm_metal_stream_write(h, buf, (uint32_t)(n + 1u));
}
