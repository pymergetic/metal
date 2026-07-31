/** @file
  Metal IO device/capability table — multi-device inventory. (impl: efi|bios)
**/
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/bus/io/io.h>

#define PM_METAL_IO_DT_MAX 32u

static pm_metal_io_node_t mNodes[PM_METAL_IO_DT_MAX];
static uint32_t           mCount;
static uint32_t           mClassCount[PM_METAL_IO_CLASS_COUNT];

static int32_t LocEqual(const uint32_t *a, const uint32_t *b)
{
  return (a[0] == b[0] && a[1] == b[1] && a[2] == b[2] && a[3] == b[3]) ? 1 : 0;
}

int pm_metal_io_dt_add(const pm_metal_io_node_t *node)
{
  uint32_t i;

  if (node == NULL || node->compat == NULL) {
    return -1;
  }

  if ((uint32_t)node->class >= (uint32_t)PM_METAL_IO_CLASS_COUNT) {
    return -1;
  }

  for (i = 0; i < mCount; i++) {
    if (mNodes[i].class == node->class && strcmp(mNodes[i].compat, node->compat) == 0 &&
        LocEqual(mNodes[i].loc, node->loc)) {
      return (int32_t)i;
    }
  }

  if (mCount >= PM_METAL_IO_DT_MAX) {
    return -1;
  }

  mNodes[mCount]      = *node;
  mNodes[mCount].unit = mClassCount[node->class];
  mClassCount[node->class]++;
  mCount++;
  return (int32_t)(mCount - 1);
}

const pm_metal_io_node_t *pm_metal_io_dt_get(uint32_t id)
{
  if (id >= mCount) {
    return NULL;
  }

  return &mNodes[id];
}

uint32_t pm_metal_io_dt_count(void)
{
  return mCount;
}

uint32_t pm_metal_io_dt_count_class(pm_metal_io_class_t class)
{
  if ((uint32_t) class >= (uint32_t)PM_METAL_IO_CLASS_COUNT) {
    return 0;
  }

  return mClassCount[class];
}

const pm_metal_io_node_t *pm_metal_io_dt_by_class(pm_metal_io_class_t class, uint32_t index)
{
  uint32_t i;
  uint32_t seen;

  if ((uint32_t) class >= (uint32_t)PM_METAL_IO_CLASS_COUNT) {
    return NULL;
  }

  seen = 0;
  for (i = 0; i < mCount; i++) {
    if (mNodes[i].class != class) {
      continue;
    }

    if (seen == index) {
      return &mNodes[i];
    }

    seen++;
  }

  return NULL;
}

const pm_metal_io_node_t *pm_metal_io_dt_lookup(pm_metal_io_class_t class)
{
  return pm_metal_io_dt_by_class(class, 0);
}

int pm_metal_io_dt_set_compat(pm_metal_io_class_t class, uint32_t index, const char *compat)
{
  uint32_t i;
  uint32_t seen;

  if (compat == NULL || (uint32_t) class >= (uint32_t)PM_METAL_IO_CLASS_COUNT) {
    return -1;
  }

  seen = 0;
  for (i = 0; i < mCount; i++) {
    if (mNodes[i].class != class) {
      continue;
    }

    if (seen == index) {
      mNodes[i].compat = compat;
      return 0;
    }

    seen++;
  }

  return -1;
}

void pm_metal_io_dt_foreach(pm_metal_io_dt_iter_fn fn, void *ctx)
{
  uint32_t i;

  if (fn == NULL) {
    return;
  }

  for (i = 0; i < mCount; i++) {
    if (fn(&mNodes[i], ctx) != 0) {
      return;
    }
  }
}

void pm_metal_io_dt_reset(void)
{
  memset(mNodes, 0, sizeof(mNodes));
  memset(mClassCount, 0, sizeof(mClassCount));
  mCount = 0;
}
