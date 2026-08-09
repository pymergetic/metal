/* virtio-gpu — stub until bus exposes GPU + virtq_add2. */
#include <stddef.h>

#include <pymergetic/metal/dev/gfx/scanout.h>

static int32_t VgpuProbe(const pm_metal_scanout_bind_t *b)
{
  (void)b;
  return -1;
}

static int32_t VgpuPresentRect(int32_t x, int32_t y, int32_t w, int32_t h)
{
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  return -1;
}

static int32_t VgpuJobBegin(int32_t x, int32_t y, int32_t w, int32_t h)
{
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  return -1;
}

static int32_t VgpuJobStep(void)
{
  return 0;
}

static uint32_t VgpuCaps(void)
{
  return 0;
}

static void VgpuFini(void) {}

const pm_metal_scanout_ops_t g_pm_metal_scanout_virtio_gpu = {
    "virtio_gpu", VgpuProbe, VgpuPresentRect, VgpuJobBegin, VgpuJobStep,
    VgpuCaps,     NULL,      NULL,            VgpuFini};
