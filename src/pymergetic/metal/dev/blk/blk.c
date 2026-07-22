/** @file
  Metal blk facade — multi-device table + guest async I/O. (impl: efi|bios)
**/
#include <pymergetic/metal/dev/blk/blk.h>
#include <pymergetic/metal/dev/blk/blk_ops.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <runtime/coro/coro.h>
#include <runtime/time/time.h>

#include <Uefi.h>
#include <Library/BaseMemoryLib.h>

#include "wasm_export.h"

#define PM_METAL_BLK_MAX  8u
#define PM_METAL_BLK_SEC  512u

STATIC pm_metal_blk_ops_t  mDevs[PM_METAL_BLK_MAX];
STATIC UINT32              mCount;
STATIC wasm_module_inst_t  mBlkInst;

void
pm_metal_blk_bind_inst (
  VOID  *module_inst
  )
{
  mBlkInst = (wasm_module_inst_t)module_inst;
}

pm_metal_blk_h
pm_metal_blk_bind (
  CONST pm_metal_blk_ops_t  *ops
  )
{
  if (ops == NULL || ops->compat == NULL || mCount >= PM_METAL_BLK_MAX) {
    return PM_METAL_BLK_INVALID;
  }

  mDevs[mCount] = *ops;
  mCount++;
  return (pm_metal_blk_h)(mCount - 1);
}

uint32_t
pm_metal_blk_count (
  VOID
  )
{
  return mCount;
}

pm_metal_blk_h
pm_metal_blk_at (
  UINT32  index
  )
{
  if (index >= mCount) {
    return PM_METAL_BLK_INVALID;
  }

  return (pm_metal_blk_h)index;
}

int
pm_metal_blk_ready (
  pm_metal_blk_h  h
  )
{
  if (h >= mCount) {
    return 0;
  }

  if (mDevs[h].ready == NULL) {
    return 1;
  }

  return mDevs[h].ready (mDevs[h].ctx) ? 1 : 0;
}

uint64_t
pm_metal_blk_capacity_sectors (
  pm_metal_blk_h  h
  )
{
  if (h >= mCount || mDevs[h].capacity == NULL) {
    return 0;
  }

  return mDevs[h].capacity (mDevs[h].ctx);
}

int
pm_metal_blk_read (
  pm_metal_blk_h  h,
  uint64_t        lba,
  VOID           *buf,
  uint32_t        nsec
  )
{
  if (h >= mCount || mDevs[h].read == NULL) {
    return -1;
  }

  return mDevs[h].read (mDevs[h].ctx, lba, buf, nsec);
}

int
pm_metal_blk_write (
  pm_metal_blk_h  h,
  uint64_t        lba,
  CONST VOID     *buf,
  uint32_t        nsec
  )
{
  if (h >= mCount || mDevs[h].write == NULL) {
    return -1;
  }

  return mDevs[h].write (mDevs[h].ctx, lba, buf, nsec);
}

void
pm_metal_blk_poll (
  VOID
  )
{
  UINT32  i;

  for (i = 0; i < mCount; i++) {
    if (mDevs[i].poll != NULL) {
      mDevs[i].poll (mDevs[i].ctx);
    }
  }
}

/* ---- awaitable sector I/O (kick → await → finish) ---- */

typedef enum {
  PM_METAL_BLK_OP_READ = 0,
  PM_METAL_BLK_OP_WRITE
} pm_metal_blk_op_t;

typedef enum {
  PM_METAL_BLK_ST_START = 0,
  PM_METAL_BLK_ST_WAIT,
  PM_METAL_BLK_ST_FINISH
} pm_metal_blk_st_t;

typedef struct {
  pm_metal_coro_t   coro;
  pm_metal_blk_op_t op;
  pm_metal_blk_h    h;
  UINT64            lba;
  UINT32            buf;
  UINT32            nsec;
  pm_metal_blk_st_t step;
  VOID             *native;
  UINT64            deadline;
  UINT8             cookie[PM_METAL_BLK_XFER_COOKIE_BYTES];
} pm_metal_blk_coro_t;

STATIC
pm_metal_status_t
MetalBlkCoroFn (
  pm_metal_coro_t  *self
  )
{
  pm_metal_blk_coro_t  *b;
  pm_metal_blk_ops_t   *ops;
  UINT32                bytes;
  INT32                 r;

  b = (pm_metal_blk_coro_t *)self;
  if (b->h >= mCount) {
    self->result = 0;
    return PM_METAL_DONE;
  }

  ops = &mDevs[b->h];

  if (b->step == PM_METAL_BLK_ST_START) {
    if (mBlkInst == NULL || b->nsec == 0 || b->buf == 0
        || pm_metal_blk_ready (b->h) == 0
        || b->nsec > (0xffffffffu / PM_METAL_BLK_SEC))
    {
      self->result = 0;
      return PM_METAL_DONE;
    }

    bytes = b->nsec * PM_METAL_BLK_SEC;
    if (!wasm_runtime_validate_app_addr (mBlkInst, b->buf, bytes)) {
      self->result = 0;
      return PM_METAL_DONE;
    }

    b->native = wasm_runtime_addr_app_to_native (mBlkInst, b->buf);
    if (b->native == NULL) {
      self->result = 0;
      return PM_METAL_DONE;
    }

    /* Legacy backend: one-shot sync (should not happen for virtio/ide). */
    if (ops->xfer_start == NULL || ops->xfer_poll == NULL
        || ops->xfer_finish == NULL)
    {
      if (b->op == PM_METAL_BLK_OP_READ) {
        self->result = (VOID *)(UINTN)(
                         (pm_metal_blk_read (b->h, b->lba, b->native, b->nsec)
                          == 0) ? b->nsec : 0u
                         );
      } else {
        self->result = (VOID *)(UINTN)(
                         (pm_metal_blk_write (b->h, b->lba, b->native, b->nsec)
                          == 0) ? b->nsec : 0u
                         );
      }

      return PM_METAL_DONE;
    }

    ZeroMem (b->cookie, sizeof (b->cookie));
    if (ops->xfer_start (
          ops->ctx,
          (b->op == PM_METAL_BLK_OP_WRITE) ? 1 : 0,
          b->lba,
          b->native,
          b->nsec,
          b->cookie
          ) != 0)
    {
      self->result = 0;
      return PM_METAL_DONE;
    }

    b->deadline = pm_metal_time_mono_us () + 5000000ull;
    b->step     = PM_METAL_BLK_ST_WAIT;
  }

  if (b->step == PM_METAL_BLK_ST_WAIT) {
    if (ops->poll != NULL) {
      ops->poll (ops->ctx);
    }

    r = ops->xfer_poll (ops->ctx, b->cookie);
    if (r < 0 || pm_metal_time_mono_us () > b->deadline) {
      (VOID)ops->xfer_finish (ops->ctx, b->cookie);
      self->result = 0;
      return PM_METAL_DONE;
    }

    if (r == 0) {
      return pm_metal_await (self, pm_metal_sleep_us (50));
    }

    b->step = PM_METAL_BLK_ST_FINISH;
  }

  if (ops->xfer_finish (ops->ctx, b->cookie) != 0) {
    self->result = 0;
  } else {
    self->result = (VOID *)(UINTN)b->nsec;
  }

  return PM_METAL_DONE;
}

STATIC
pm_metal_async_handle_t
MetalBlkStart (
  pm_metal_blk_op_t  op,
  pm_metal_blk_h     h,
  UINT64             lba,
  UINT32             buf,
  UINT32             nsec
  )
{
  pm_metal_blk_coro_t  *b;

  if (h == PM_METAL_BLK_INVALID || nsec == 0 || buf == 0) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  b = (pm_metal_blk_coro_t *)pm_metal_coro (MetalBlkCoroFn, sizeof (*b));
  if (b == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  b->op   = op;
  b->h    = h;
  b->lba  = lba;
  b->buf  = buf;
  b->nsec = nsec;
  b->step = PM_METAL_BLK_ST_START;
  return pm_metal_async_adopt_host_coro (&b->coro);
}

pm_metal_async_handle_t
pm_metal_blk_read_async (
  pm_metal_blk_h  h,
  uint64_t        lba,
  uint32_t        dest,
  uint32_t        nsec
  )
{
  return MetalBlkStart (PM_METAL_BLK_OP_READ, h, lba, dest, nsec);
}

pm_metal_async_handle_t
pm_metal_blk_write_async (
  pm_metal_blk_h  h,
  uint64_t        lba,
  uint32_t        src,
  uint32_t        nsec
  )
{
  return MetalBlkStart (PM_METAL_BLK_OP_WRITE, h, lba, src, nsec);
}

uint32_t
pm_metal_blk_result (
  pm_metal_async_handle_t  self_h
  )
{
  return pm_metal_async_result_u32 (self_h);
}

STATIC UINT32
pm_metal_blk_count_native (
  wasm_exec_env_t  exec_env
  )
{
  (VOID)exec_env;
  return pm_metal_blk_count ();
}

STATIC UINT32
pm_metal_blk_at_native (
  wasm_exec_env_t  exec_env,
  UINT32           index
  )
{
  (VOID)exec_env;
  return pm_metal_blk_at (index);
}

STATIC INT32
pm_metal_blk_ready_native (
  wasm_exec_env_t  exec_env,
  UINT32           h
  )
{
  (VOID)exec_env;
  return pm_metal_blk_ready (h);
}

STATIC UINT64
pm_metal_blk_capacity_sectors_native (
  wasm_exec_env_t  exec_env,
  UINT32           h
  )
{
  (VOID)exec_env;
  return pm_metal_blk_capacity_sectors (h);
}

STATIC UINT32
pm_metal_blk_read_async_native (
  wasm_exec_env_t  exec_env,
  UINT32           h,
  UINT64           lba,
  UINT32           dest,
  UINT32           nsec
  )
{
  (VOID)exec_env;
  return pm_metal_blk_read_async (h, lba, dest, nsec);
}

STATIC UINT32
pm_metal_blk_write_async_native (
  wasm_exec_env_t  exec_env,
  UINT32           h,
  UINT64           lba,
  UINT32           src,
  UINT32           nsec
  )
{
  (VOID)exec_env;
  return pm_metal_blk_write_async (h, lba, src, nsec);
}

STATIC UINT32
pm_metal_blk_result_native (
  wasm_exec_env_t  exec_env,
  UINT32           self_h
  )
{
  (VOID)exec_env;
  return pm_metal_blk_result (self_h);
}

STATIC NativeSymbol g_pm_metal_blk_native_symbols[] = {
  { "pm_metal_blk_count", (VOID *)pm_metal_blk_count_native, "()i", NULL },
  { "pm_metal_blk_at", (VOID *)pm_metal_blk_at_native, "(i)i", NULL },
  { "pm_metal_blk_ready", (VOID *)pm_metal_blk_ready_native, "(i)i", NULL },
  { "pm_metal_blk_capacity_sectors", (VOID *)pm_metal_blk_capacity_sectors_native,
    "(i)I", NULL },
  { "pm_metal_blk_read_async", (VOID *)pm_metal_blk_read_async_native, "(iIii)i",
    NULL },
  { "pm_metal_blk_write_async", (VOID *)pm_metal_blk_write_async_native, "(iIii)i",
    NULL },
  { "pm_metal_blk_result", (VOID *)pm_metal_blk_result_native, "(i)i", NULL },
};

int
pm_metal_blk_native_register (
  VOID
  )
{
  if (!wasm_runtime_register_natives (
         PM_METAL_BLK_WASI_MODULE,
         g_pm_metal_blk_native_symbols,
         sizeof (g_pm_metal_blk_native_symbols)
           / sizeof (g_pm_metal_blk_native_symbols[0])
         ))
  {
    return -1;
  }

  return 0;
}
