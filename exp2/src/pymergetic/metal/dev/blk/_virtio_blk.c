#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/async/coro.h>
#include <pymergetic/metal/async/handle.h>
#include <pymergetic/metal/async/task.h>
#include <pymergetic/metal/async/time.h>
#include <pymergetic/metal/bus/virtio/__init__.h>
#include <pymergetic/metal/dev/blk/__init__.h>

#define VBLK_Q 0u
#define VBLK_QSZ 64u
#define VBLK_MAX_SECTORS 8u
#define VBLK_TIMEOUT_US 5000000ull

#define VIRTIO_BLK_T_IN 0u
#define VIRTIO_BLK_S_OK 0u

#pragma pack(1)
typedef struct {
  uint32_t type;
  uint32_t reserved;
  uint64_t sector;
} vblk_req_t;
#pragma pack()

typedef struct {
  pm_metal_virtio_dev_t dev;
  uint64_t capacity;
  vblk_req_t *req;
  uint8_t *data;
  uint8_t *status;
  uint16_t head;
  uint8_t ready;
  uint8_t busy;
  uint8_t used;
} vblk_dev_t;

typedef struct {
  uint64_t lba;
  void *buf;
  uint32_t nsec;
  uint64_t deadline;
  uint8_t submitted;
} vblk_async_t;

static vblk_dev_t mVblk;

static int32_t vblk_submit(uint64_t lba, uint32_t nsec)
{
  uint32_t bytes;

  if (!mVblk.ready || mVblk.busy || nsec == 0u || nsec > VBLK_MAX_SECTORS ||
      lba >= mVblk.capacity || nsec > mVblk.capacity - lba) {
    return -1;
  }
  bytes = nsec * PM_METAL_DEV_BLK_SECTOR_BYTES;
  memset(mVblk.req, 0, sizeof(*mVblk.req));
  mVblk.req->type = VIRTIO_BLK_T_IN;
  mVblk.req->sector = lba;
  memset(mVblk.data, 0, bytes);
  *mVblk.status = 0xffu;
  mVblk.used = 0u;
  if (pm_metal_virtq_add3(&mVblk.dev.vqs[VBLK_Q],
                          mVblk.req,
                          (uint32_t)sizeof(*mVblk.req),
                          0,
                          mVblk.data,
                          bytes,
                          1,
                          mVblk.status,
                          1,
                          1,
                          &mVblk.head) != 0) {
    return -1;
  }
  mVblk.busy = 1u;
  pm_metal_virtq_kick(&mVblk.dev, &mVblk.dev.vqs[VBLK_Q]);
  return 0;
}

static int32_t vblk_poll(void)
{
  uint16_t head;
  uint32_t len;

  if (!mVblk.busy) {
    return -1;
  }
  if (mVblk.used) {
    return 1;
  }
  pm_metal_virtio_ack_isr(&mVblk.dev);
  if (!pm_metal_virtq_get_used(&mVblk.dev.vqs[VBLK_Q], &head, &len)) {
    return 0;
  }
  (void)len;
  mVblk.head = head;
  mVblk.used = 1u;
  return 1;
}

static int32_t vblk_finish(void *buf, uint32_t nsec)
{
  uint32_t bytes;
  int32_t ok;

  if (!mVblk.busy || !mVblk.used || buf == NULL) {
    return -1;
  }
  bytes = nsec * PM_METAL_DEV_BLK_SECTOR_BYTES;
  ok = (*mVblk.status == VIRTIO_BLK_S_OK) ? 0 : -1;
  if (ok == 0) {
    memcpy(buf, mVblk.data, bytes);
  }
  pm_metal_virtq_free_chain(&mVblk.dev.vqs[VBLK_Q], mVblk.head);
  mVblk.busy = 0u;
  mVblk.used = 0u;
  return ok;
}

int32_t pm_metal_dev_blk_open(void)
{
  uint64_t features;
  uint8_t cap[8];

  if (mVblk.ready) {
    return 0;
  }
  if (pm_metal_virtio_open(PM_METAL_VIRTIO_DEV_BLK, &mVblk.dev) != 0 &&
      pm_metal_virtio_open(PM_METAL_VIRTIO_DEV_BLK_LEGACY, &mVblk.dev) != 0) {
    return -1;
  }
  features = pm_metal_virtio_get_features(&mVblk.dev) & PM_METAL_VIRTIO_F_VERSION_1;
  if (pm_metal_virtio_set_features(&mVblk.dev, features) != 0) {
    pm_metal_virtio_set_status(&mVblk.dev, 0u);
    pm_metal_virtio_set_status(
      &mVblk.dev, (uint8_t)(PM_METAL_VIRTIO_S_ACK | PM_METAL_VIRTIO_S_DRIVER));
    if (pm_metal_virtio_set_features(&mVblk.dev, 0u) != 0) {
      pm_metal_virtio_close(&mVblk.dev);
      return -1;
    }
  }
  memset(cap, 0, sizeof(cap));
  if (pm_metal_virtio_cfg_read(&mVblk.dev, 0u, cap, sizeof(cap)) != 0) {
    pm_metal_virtio_close(&mVblk.dev);
    return -1;
  }
  mVblk.capacity = (uint64_t)cap[0] | ((uint64_t)cap[1] << 8) |
                   ((uint64_t)cap[2] << 16) | ((uint64_t)cap[3] << 24) |
                   ((uint64_t)cap[4] << 32) | ((uint64_t)cap[5] << 40) |
                   ((uint64_t)cap[6] << 48) | ((uint64_t)cap[7] << 56);
  if (mVblk.capacity == 0u ||
      pm_metal_virtio_setup_queue(&mVblk.dev, VBLK_Q, VBLK_QSZ) != 0) {
    pm_metal_virtio_close(&mVblk.dev);
    return -1;
  }
  mVblk.req = pm_metal_virtio_pages_alloc(1u);
  mVblk.data = pm_metal_virtio_pages_alloc(
    PM_METAL_VIRTIO_SIZE_TO_PAGES(PM_METAL_DEV_BLK_SECTOR_BYTES * VBLK_MAX_SECTORS));
  mVblk.status = pm_metal_virtio_pages_alloc(1u);
  if (mVblk.req == NULL || mVblk.data == NULL || mVblk.status == NULL) {
    pm_metal_virtio_close(&mVblk.dev);
    return -1;
  }
  if (pm_metal_virtio_driver_ok(&mVblk.dev) != 0) {
    pm_metal_virtio_close(&mVblk.dev);
    return -1;
  }
  mVblk.ready = 1u;
  return 0;
}

uint64_t pm_metal_dev_blk_capacity_sectors(void)
{
  return mVblk.ready ? mVblk.capacity : 0u;
}

int32_t pm_metal_dev_blk_read(uint64_t lba, void *buf, uint32_t nsec)
{
  uint64_t deadline;

  if (buf == NULL || vblk_submit(lba, nsec) != 0) {
    return -1;
  }
  deadline = pm_metal_time_mono_us() + VBLK_TIMEOUT_US;
  while (pm_metal_time_mono_us() < deadline) {
    int32_t status = vblk_poll();

    if (status < 0) {
      return -1;
    }
    if (status > 0) {
      return vblk_finish(buf, nsec);
    }
  }
  return -1;
}

static uint32_t vblk_async_step(uint32_t self_h)
{
  vblk_async_t *state;
  int32_t status;

  state = (vblk_async_t *)pm_metal_async_coro_state(self_h);
  if (state == NULL) {
    return PM_METAL_ASYNC_ERROR;
  }
  if (!state->submitted) {
    if (state->buf == NULL || vblk_submit(state->lba, state->nsec) != 0) {
      pm_metal_async_set_result_u32(self_h, 0u);
      return PM_METAL_ASYNC_ERROR;
    }
    state->submitted = 1u;
    state->deadline = pm_metal_time_mono_us() + VBLK_TIMEOUT_US;
  }
  status = vblk_poll();
  if (status < 0 || pm_metal_time_mono_us() >= state->deadline) {
    pm_metal_async_set_result_u32(self_h, 0u);
    return PM_METAL_ASYNC_ERROR;
  }
  if (status == 0) {
    return PM_METAL_ASYNC_PENDING;
  }
  if (vblk_finish(state->buf, state->nsec) != 0) {
    pm_metal_async_set_result_u32(self_h, 0u);
    return PM_METAL_ASYNC_ERROR;
  }
  pm_metal_async_set_result_u32(self_h, state->nsec);
  return PM_METAL_ASYNC_DONE;
}

uint32_t pm_metal_dev_blk_read_async(uint64_t lba, void *buf, uint32_t nsec)
{
  vblk_async_t *state;
  uint32_t h;

  if (buf == NULL || nsec == 0u || nsec > VBLK_MAX_SECTORS) {
    return PM_METAL_DEV_BLK_INVALID;
  }
  h = pm_metal_async_spawn(
    vblk_async_step, (uint32_t)sizeof(*state), PM_METAL_ASYNC_PRIO_MED);
  if (h == PM_METAL_DEV_BLK_INVALID) {
    return PM_METAL_DEV_BLK_INVALID;
  }
  state = (vblk_async_t *)pm_metal_async_coro_state(h);
  if (state == NULL) {
    pm_metal_async_coro_close(h);
    return PM_METAL_DEV_BLK_INVALID;
  }
  state->lba = lba;
  state->buf = buf;
  state->nsec = nsec;
  return h;
}

uint32_t pm_metal_dev_blk_result(uint32_t h)
{
  return pm_metal_async_result_u32(h);
}
