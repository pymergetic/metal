/** @file
  IDE/PATA PIO detector + driver. Registers each found drive in DT.
  (impl: efi|bios)
**/
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/dev/blk/blk.h>
#include <pymergetic/metal/dev/blk/blk_ops.h>
#include <pymergetic/metal/bus/io/io.h>
#include <runtime/io/io.h>
#include <runtime/time/cpu.h>
#include <runtime/time/time.h>

#define IDE_SEC        512u
#define IDE_MAX_DRIVES 4u

#define ATA_REG_DATA     0u
#define ATA_REG_ERROR    1u
#define ATA_REG_SECCOUNT 2u
#define ATA_REG_LBA_LO   3u
#define ATA_REG_LBA_MID  4u
#define ATA_REG_LBA_HI   5u
#define ATA_REG_DRIVE    6u
#define ATA_REG_STATUS   7u
#define ATA_REG_CMD      7u

#define ATA_SR_ERR  0x01u
#define ATA_SR_DRQ  0x08u
#define ATA_SR_DF   0x20u
#define ATA_SR_DRDY 0x40u
#define ATA_SR_BSY  0x80u

#define ATA_CMD_IDENTIFY  0xECu
#define ATA_CMD_READ_PIO  0x20u
#define ATA_CMD_WRITE_PIO 0x30u

typedef struct {
  uint16_t CmdBase;
  uint16_t CtrlBase;
  uint8_t  Drive; /* 0 master, 1 slave */
  int32_t  Ready;
  uint64_t Capacity;
  uint16_t Identify[256];
} ide_drive_t;

static ide_drive_t mDrives[IDE_MAX_DRIVES];
static uint32_t    mDriveCount;

static void IdeDelay(uint32_t us)
{
  pm_metal_time_usleep(us);
}

static uint8_t IdeStatus(const ide_drive_t *d)
{
  return pm_metal_io_in8((uint16_t)(uintptr_t)(d->CmdBase + ATA_REG_STATUS));
}

static int32_t IdeWaitNotBusy(const ide_drive_t *d, uint32_t timeout_ms)
{
  uint64_t deadline;

  deadline = pm_metal_time_mono_us() + (uint64_t)timeout_ms * 1000ull;
  while (pm_metal_time_mono_us() < deadline) {
    uint8_t st;

    st = IdeStatus(d);
    if (st == 0xff) {
      return -1;
    }

    if ((st & ATA_SR_BSY) == 0) {
      return 0;
    }

    pm_metal_cpu_pause();
  }

  return -1;
}

static int32_t IdeWaitDrq(const ide_drive_t *d, uint32_t timeout_ms)
{
  uint64_t deadline;

  deadline = pm_metal_time_mono_us() + (uint64_t)timeout_ms * 1000ull;
  while (pm_metal_time_mono_us() < deadline) {
    uint8_t st;

    st = IdeStatus(d);
    if (st == 0xff) {
      return -1;
    }

    if ((st & ATA_SR_BSY) != 0) {
      pm_metal_cpu_pause();
      continue;
    }

    if ((st & (ATA_SR_ERR | ATA_SR_DF)) != 0) {
      return -1;
    }

    if ((st & ATA_SR_DRQ) != 0) {
      return 0;
    }

    pm_metal_cpu_pause();
  }

  return -1;
}

static void IdeSelect(const ide_drive_t *d)
{
  pm_metal_io_out8((uint16_t)(uintptr_t)(d->CmdBase + ATA_REG_DRIVE),
                   (uint8_t)(0xA0u | ((uint8_t)(d->Drive & 1u) << 4)));
  IdeDelay(400);
}

static int32_t IdeIdentify(ide_drive_t *d)
{
  uint32_t i;
  uint8_t  st;

  IdeSelect(d);
  st = IdeStatus(d);
  if (st == 0xff || st == 0x00) {
    return -1;
  }

  pm_metal_io_out8((uint16_t)(uintptr_t)(d->CmdBase + ATA_REG_SECCOUNT), 0);
  pm_metal_io_out8((uint16_t)(uintptr_t)(d->CmdBase + ATA_REG_LBA_LO), 0);
  pm_metal_io_out8((uint16_t)(uintptr_t)(d->CmdBase + ATA_REG_LBA_MID), 0);
  pm_metal_io_out8((uint16_t)(uintptr_t)(d->CmdBase + ATA_REG_LBA_HI), 0);
  pm_metal_io_out8((uint16_t)(uintptr_t)(d->CmdBase + ATA_REG_CMD), ATA_CMD_IDENTIFY);

  IdeDelay(400);
  st = IdeStatus(d);
  if (st == 0x00 || st == 0xff) {
    return -1;
  }

  if (IdeWaitNotBusy(d, 2000) != 0) {
    return -1;
  }

  /* ATAPI: mid/hi non-zero after IDENTIFY */
  if (pm_metal_io_in8((uint16_t)(uintptr_t)(d->CmdBase + ATA_REG_LBA_MID)) != 0 ||
      pm_metal_io_in8((uint16_t)(uintptr_t)(d->CmdBase + ATA_REG_LBA_HI)) != 0) {
    return -1;
  }

  if (IdeWaitDrq(d, 2000) != 0) {
    return -1;
  }

  for (i = 0; i < 256; i++) {
    d->Identify[i] = pm_metal_io_in16((uint16_t)(uintptr_t)(d->CmdBase + ATA_REG_DATA));
  }

  d->Capacity = (uint64_t)d->Identify[60] | ((uint64_t)d->Identify[61] << 16);
  if (d->Capacity == 0) {
    return -1;
  }

  d->Ready = 1;
  return 0;
}

typedef struct {
  uint32_t  write;
  uint32_t  nsec;
  uint32_t  sec;
  uint16_t *words;
  uint8_t   phase; /* 0 issued; 1 sector PIO; 2 final !BSY */
} ide_xfer_cookie_t;

static int IdeXferStart(void *ctx, int write, uint64_t lba, void *buf, uint32_t nsec, void *cookie)
{
  ide_drive_t       *d;
  ide_xfer_cookie_t *c;
  uint8_t            st;

  d = (ide_drive_t *)ctx;
  c = (ide_xfer_cookie_t *)cookie;
  if (d == NULL || c == NULL || !d->Ready || buf == NULL || nsec == 0 || nsec > 256 ||
      lba + nsec > d->Capacity || (lba >> 28) != 0) {
    return -1;
  }

  c->write = write ? 1u : 0u;
  c->nsec  = nsec;
  c->sec   = 0;
  c->words = (uint16_t *)buf;
  c->phase = 0;

  IdeSelect(d);
  st = IdeStatus(d);
  if (st == 0xff || (st & ATA_SR_BSY) != 0) {
    return -1;
  }

  pm_metal_io_out8((uint16_t)(uintptr_t)(d->CmdBase + ATA_REG_SECCOUNT), (uint8_t)nsec);
  pm_metal_io_out8((uint16_t)(uintptr_t)(d->CmdBase + ATA_REG_LBA_LO), (uint8_t)(lba & 0xffu));
  pm_metal_io_out8((uint16_t)(uintptr_t)(d->CmdBase + ATA_REG_LBA_MID),
                   (uint8_t)((lba >> 8) & 0xffu));
  pm_metal_io_out8((uint16_t)(uintptr_t)(d->CmdBase + ATA_REG_LBA_HI),
                   (uint8_t)((lba >> 16) & 0xffu));
  pm_metal_io_out8(
    (uint16_t)(uintptr_t)(d->CmdBase + ATA_REG_DRIVE),
    (uint8_t)(0xE0u | ((uint8_t)(d->Drive & 1u) << 4) | (uint8_t)((lba >> 24) & 0x0fu)));
  pm_metal_io_out8((uint16_t)(uintptr_t)(d->CmdBase + ATA_REG_CMD),
                   write ? ATA_CMD_WRITE_PIO : ATA_CMD_READ_PIO);
  c->phase = 1;
  return 0;
}

static int IdeXferPoll(void *ctx, void *cookie)
{
  ide_drive_t       *d;
  ide_xfer_cookie_t *c;
  uint8_t            st;
  uint32_t           i;

  d = (ide_drive_t *)ctx;
  c = (ide_xfer_cookie_t *)cookie;
  if (d == NULL || c == NULL) {
    return -1;
  }

  st = IdeStatus(d);
  if (st == 0xff) {
    return -1;
  }

  if (c->phase == 1) {
    if ((st & ATA_SR_BSY) != 0) {
      return 0;
    }

    if ((st & (ATA_SR_ERR | ATA_SR_DF)) != 0) {
      return -1;
    }

    if ((st & ATA_SR_DRQ) == 0) {
      return 0;
    }

    if (c->write) {
      for (i = 0; i < 256; i++) {
        pm_metal_io_out16((uint16_t)(uintptr_t)(d->CmdBase + ATA_REG_DATA),
                          c->words[c->sec * 256u + i]);
      }
    } else {
      for (i = 0; i < 256; i++) {
        c->words[c->sec * 256u + i] =
          pm_metal_io_in16((uint16_t)(uintptr_t)(d->CmdBase + ATA_REG_DATA));
      }
    }

    c->sec++;
    if (c->sec >= c->nsec) {
      c->phase = 2;
    }

    return 0;
  }

  if (c->phase == 2) {
    if ((st & ATA_SR_BSY) != 0) {
      return 0;
    }

    if ((st & (ATA_SR_ERR | ATA_SR_DF)) != 0) {
      return -1;
    }

    return 1;
  }

  return -1;
}

static int IdeXferFinish(void *ctx, void *cookie)
{
  (void)ctx;
  (void)cookie;
  return 0;
}

static int32_t IdePioXfer(ide_drive_t *d, uint32_t Write, uint64_t Lba, void *Buf, uint32_t Nsec)
{
  ide_xfer_cookie_t cookie;
  uint64_t          deadline;

  /* Sync path for IDENTIFY-era / boot: busy-poll, no sleep. */
  if (d == NULL || !d->Ready) {
    return -1;
  }

  IdeSelect(d);
  if (IdeWaitNotBusy(d, 2000) != 0) {
    return -1;
  }

  memset(&cookie, 0, sizeof(cookie));
  if (IdeXferStart(d, (int32_t)Write, Lba, Buf, Nsec, &cookie) != 0) {
    return -1;
  }

  deadline = pm_metal_time_mono_us() + 5000000ull;
  while (pm_metal_time_mono_us() < deadline) {
    int32_t r;

    r = IdeXferPoll(d, &cookie);
    if (r < 0) {
      return -1;
    }

    if (r > 0) {
      return IdeXferFinish(d, &cookie);
    }

    pm_metal_cpu_pause();
  }

  return -1;
}

static int IdeReady(void *ctx)
{
  ide_drive_t *d;

  d = (ide_drive_t *)ctx;
  return (d != NULL && d->Ready) ? 1 : 0;
}

static uint64_t IdeCapacity(void *ctx)
{
  ide_drive_t *d;

  d = (ide_drive_t *)ctx;
  return (d != NULL) ? d->Capacity : 0;
}

static int IdeRead(void *ctx, uint64_t lba, void *buf, uint32_t nsec)
{
  return IdePioXfer((ide_drive_t *)ctx, 0, lba, buf, nsec);
}

static int IdeWrite(void *ctx, uint64_t lba, const void *buf, uint32_t nsec)
{
  return IdePioXfer((ide_drive_t *)ctx, 1, lba, (void *)buf, nsec);
}

static int32_t IdeBindDrive(ide_drive_t *d)
{
  pm_metal_io_node_t Node;
  pm_metal_blk_ops_t Ops;
  int32_t            dt_id;

  memset(&Node, 0, sizeof(Node));
  Node.class  = PM_METAL_IO_BLK;
  Node.compat = "ide-ata";
  Node.caps   = 1;
  Node.bus    = PM_METAL_IO_BUS_ISA;
  Node.loc[0] = d->CmdBase;
  Node.loc[1] = d->CtrlBase;
  Node.loc[2] = d->Drive;
  Node.loc[3] = 0;
  dt_id       = pm_metal_io_dt_add(&Node);
  if (dt_id < 0) {
    return -1;
  }

  memset(&Ops, 0, sizeof(Ops));
  Ops.compat      = "ide-ata";
  Ops.dt_id       = (uint32_t)dt_id;
  Ops.ready       = IdeReady;
  Ops.capacity    = IdeCapacity;
  Ops.read        = IdeRead;
  Ops.write       = IdeWrite;
  Ops.xfer_start  = IdeXferStart;
  Ops.xfer_poll   = IdeXferPoll;
  Ops.xfer_finish = IdeXferFinish;
  Ops.ctx         = d;
  if (pm_metal_blk_bind(&Ops) == PM_METAL_BLK_INVALID) {
    return -1;
  }

  return 0;
}

static void IdeProbeChannel(uint16_t CmdBase, uint16_t CtrlBase)
{
  uint8_t drive;

  for (drive = 0; drive < 2; drive++) {
    ide_drive_t *d;

    if (mDriveCount >= IDE_MAX_DRIVES) {
      return;
    }

    d = &mDrives[mDriveCount];
    memset(d, 0, sizeof(*d));
    d->CmdBase  = CmdBase;
    d->CtrlBase = CtrlBase;
    d->Drive    = drive;
    if (IdeIdentify(d) != 0) {
      continue;
    }

    if (IdeBindDrive(d) != 0) {
      d->Ready = 0;
      continue;
    }

    mDriveCount++;
  }
}

int pm_metal_blk_ide_detect(void)
{
  /* Legacy ISA command blocks — present on ICH4-M / PIIX / QEMU IDE. */
  IdeProbeChannel(0x1F0, 0x3F6);
  IdeProbeChannel(0x170, 0x376);
  return (mDriveCount > 0) ? 0 : -1;
}
