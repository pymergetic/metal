/** @file
  Metal blk facade — multi-device table + guest async I/O. (impl: efi|bios)
**/
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/dev/blk/blk.h>
#include <pymergetic/metal/dev/blk/blk_ops.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <runtime/time/time.h>

#include "wasm_export.h"

#define PM_METAL_BLK_MAX 8u
#define PM_METAL_BLK_SEC 512u

static pm_metal_blk_ops_t mDevs[PM_METAL_BLK_MAX];
static uint32_t           mCount;
static wasm_module_inst_t mBlkInst;

void pm_metal_blk_bind_inst(void *module_inst)
{
  mBlkInst = (wasm_module_inst_t)module_inst;
}

pm_metal_blk_h pm_metal_blk_bind(const pm_metal_blk_ops_t *ops)
{
  if (ops == NULL || ops->compat == NULL || mCount >= PM_METAL_BLK_MAX) {
    return PM_METAL_BLK_INVALID;
  }

  mDevs[mCount] = *ops;
  mCount++;
  return (pm_metal_blk_h)(mCount - 1);
}

uint32_t pm_metal_blk_count(void)
{
  return mCount;
}

pm_metal_blk_h pm_metal_blk_at(uint32_t index)
{
  if (index >= mCount) {
    return PM_METAL_BLK_INVALID;
  }

  return (pm_metal_blk_h)index;
}

int pm_metal_blk_ready(pm_metal_blk_h h)
{
  if (h >= mCount) {
    return 0;
  }

  if (mDevs[h].ready == NULL) {
    return 1;
  }

  return mDevs[h].ready(mDevs[h].ctx) ? 1 : 0;
}

uint64_t pm_metal_blk_capacity_sectors(pm_metal_blk_h h)
{
  if (h >= mCount || mDevs[h].capacity == NULL) {
    return 0;
  }

  return mDevs[h].capacity(mDevs[h].ctx);
}

int pm_metal_blk_read(pm_metal_blk_h h, uint64_t lba, void *buf, uint32_t nsec)
{
  if (h >= mCount || mDevs[h].read == NULL) {
    return -1;
  }

  return mDevs[h].read(mDevs[h].ctx, lba, buf, nsec);
}

int pm_metal_blk_write(pm_metal_blk_h h, uint64_t lba, const void *buf, uint32_t nsec)
{
  if (h >= mCount || mDevs[h].write == NULL) {
    return -1;
  }

  return mDevs[h].write(mDevs[h].ctx, lba, buf, nsec);
}

void pm_metal_blk_poll(void)
{
  uint32_t i;

  for (i = 0; i < mCount; i++) {
    if (mDevs[i].poll != NULL) {
      mDevs[i].poll(mDevs[i].ctx);
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
  pm_metal_blk_op_t op;
  pm_metal_blk_h    h;
  uint64_t          lba;
  uint32_t          buf;
  uint32_t          nsec;
  pm_metal_blk_st_t step;
  void             *native;
  uint64_t          deadline;
  uint8_t           cookie[PM_METAL_BLK_XFER_COOKIE_BYTES];
} pm_metal_blk_coro_t;

static pm_metal_status_t MetalBlkStep(pm_metal_async_handle_t self_h)
{
  pm_metal_blk_coro_t *b;
  pm_metal_blk_ops_t  *ops;
  int32_t              r;

  b = (pm_metal_blk_coro_t *)(uintptr_t)pm_metal_async_coro_state(self_h);
  if (b == NULL) {
    return PM_METAL_ERROR;
  }

  if (b->h >= mCount) {
    pm_metal_async_set_result_u32(self_h, 0);
    return PM_METAL_DONE;
  }

  ops = &mDevs[b->h];

  if (b->step == PM_METAL_BLK_ST_START) {
    if (b->native == NULL || b->nsec == 0 || pm_metal_blk_ready(b->h) == 0 ||
        b->nsec > (0xffffffffu / PM_METAL_BLK_SEC)) {
      pm_metal_async_set_result_u32(self_h, 0);
      return PM_METAL_DONE;
    }

    /* Legacy backend: one-shot sync (should not happen for virtio/ide). */
    if (ops->xfer_start == NULL || ops->xfer_poll == NULL || ops->xfer_finish == NULL) {
      if (b->op == PM_METAL_BLK_OP_READ) {
        pm_metal_async_set_result_u32(
          self_h, (pm_metal_blk_read(b->h, b->lba, b->native, b->nsec) == 0) ? b->nsec : 0u);
      } else {
        pm_metal_async_set_result_u32(
          self_h, (pm_metal_blk_write(b->h, b->lba, b->native, b->nsec) == 0) ? b->nsec : 0u);
      }

      return PM_METAL_DONE;
    }

    memset(b->cookie, 0, sizeof(b->cookie));
    if (ops->xfer_start(ops->ctx,
                        (b->op == PM_METAL_BLK_OP_WRITE) ? 1 : 0,
                        b->lba,
                        b->native,
                        b->nsec,
                        b->cookie) != 0) {
      pm_metal_async_set_result_u32(self_h, 0);
      return PM_METAL_DONE;
    }

    b->deadline = pm_metal_time_mono_us() + 5000000ull;
    b->step     = PM_METAL_BLK_ST_WAIT;
  }

  if (b->step == PM_METAL_BLK_ST_WAIT) {
    if (ops->poll != NULL) {
      ops->poll(ops->ctx);
    }

    r = ops->xfer_poll(ops->ctx, b->cookie);
    if (r < 0 || pm_metal_time_mono_us() > b->deadline) {
      (void)ops->xfer_finish(ops->ctx, b->cookie);
      pm_metal_async_set_result_u32(self_h, 0);
      return PM_METAL_DONE;
    }

    if (r == 0) {
      return pm_metal_async_await(self_h, pm_metal_async_sleep_us(50));
    }

    b->step = PM_METAL_BLK_ST_FINISH;
  }

  if (ops->xfer_finish(ops->ctx, b->cookie) != 0) {
    pm_metal_async_set_result_u32(self_h, 0);
  } else {
    pm_metal_async_set_result_u32(self_h, b->nsec);
  }

  return PM_METAL_DONE;
}

static pm_metal_async_handle_t MetalBlkStart(
  pm_metal_blk_op_t op, pm_metal_blk_h h, uint64_t lba, void *native, uint32_t nsec)
{
  pm_metal_blk_coro_t    *b;
  pm_metal_async_handle_t ah;

  if (h == PM_METAL_BLK_INVALID || nsec == 0 || native == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  ah = pm_metal_async_coro_create(MetalBlkStep, sizeof(*b));
  if (ah == PM_METAL_ASYNC_HANDLE_INVALID) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  b = (pm_metal_blk_coro_t *)(uintptr_t)pm_metal_async_coro_state(ah);
  if (b == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  b->op     = op;
  b->h      = h;
  b->lba    = lba;
  b->buf    = 0;
  b->nsec   = nsec;
  b->native = native;
  b->step   = PM_METAL_BLK_ST_START;
  return ah;
}

pm_metal_async_handle_t pm_metal_blk_read_async(pm_metal_blk_h h,
                                                uint64_t       lba,
                                                uint32_t       dest,
                                                uint32_t       nsec)
{
  void *native;

  if (nsec == 0 || nsec > (0xffffffffu / PM_METAL_BLK_SEC)) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  native = pm_metal_async_guest_buf_durable(NULL, dest, nsec * PM_METAL_BLK_SEC);
  return MetalBlkStart(PM_METAL_BLK_OP_READ, h, lba, native, nsec);
}

pm_metal_async_handle_t pm_metal_blk_write_async(pm_metal_blk_h h,
                                                 uint64_t       lba,
                                                 uint32_t       src,
                                                 uint32_t       nsec)
{
  void *native;

  if (nsec == 0 || nsec > (0xffffffffu / PM_METAL_BLK_SEC)) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  native = pm_metal_async_guest_buf_durable(NULL, src, nsec * PM_METAL_BLK_SEC);
  return MetalBlkStart(PM_METAL_BLK_OP_WRITE, h, lba, native, nsec);
}

uint32_t pm_metal_blk_result(pm_metal_async_handle_t self_h)
{
  return pm_metal_async_result_u32(self_h);
}

static uint32_t pm_metal_blk_count_native(wasm_exec_env_t exec_env)
{
  (void)exec_env;
  return pm_metal_blk_count();
}

static uint32_t pm_metal_blk_at_native(wasm_exec_env_t exec_env, uint32_t index)
{
  (void)exec_env;
  return pm_metal_blk_at(index);
}

static int32_t pm_metal_blk_ready_native(wasm_exec_env_t exec_env, uint32_t h)
{
  (void)exec_env;
  return pm_metal_blk_ready(h);
}

static uint64_t pm_metal_blk_capacity_sectors_native(wasm_exec_env_t exec_env, uint32_t h)
{
  (void)exec_env;
  return pm_metal_blk_capacity_sectors(h);
}

static uint32_t pm_metal_blk_read_async_native(
  wasm_exec_env_t exec_env, uint32_t h, uint64_t lba, uint32_t dest, uint32_t nsec)
{
  (void)exec_env;
  return pm_metal_blk_read_async(h, lba, dest, nsec);
}

static uint32_t pm_metal_blk_write_async_native(
  wasm_exec_env_t exec_env, uint32_t h, uint64_t lba, uint32_t src, uint32_t nsec)
{
  (void)exec_env;
  return pm_metal_blk_write_async(h, lba, src, nsec);
}

static uint32_t pm_metal_blk_result_native(wasm_exec_env_t exec_env, uint32_t self_h)
{
  (void)exec_env;
  return pm_metal_blk_result(self_h);
}

static NativeSymbol g_pm_metal_blk_native_symbols[] = {
  { "pm_metal_blk_count", (void *)pm_metal_blk_count_native, "()i", NULL },
  { "pm_metal_blk_at", (void *)pm_metal_blk_at_native, "(i)i", NULL },
  { "pm_metal_blk_ready", (void *)pm_metal_blk_ready_native, "(i)i", NULL },
  { "pm_metal_blk_capacity_sectors", (void *)pm_metal_blk_capacity_sectors_native, "(i)I", NULL },
  { "pm_metal_blk_read_async", (void *)pm_metal_blk_read_async_native, "(iIii)i", NULL },
  { "pm_metal_blk_write_async", (void *)pm_metal_blk_write_async_native, "(iIii)i", NULL },
  { "pm_metal_blk_result", (void *)pm_metal_blk_result_native, "(i)i", NULL },
};

int pm_metal_blk_native_register(void)
{
  if (!wasm_runtime_register_natives(PM_METAL_BLK_WASI_MODULE,
                                     g_pm_metal_blk_native_symbols,
                                     sizeof(g_pm_metal_blk_native_symbols) /
                                       sizeof(g_pm_metal_blk_native_symbols[0]))) {
    return -1;
  }

  return 0;
}
