/** @file
  IDE/PATA PIO detector + driver. Registers each found drive in DT.
  (impl: efi|bios)
**/
#include <pymergetic/metal/dev/blk/blk.h>
#include <pymergetic/metal/dev/blk/blk_ops.h>
#include <pymergetic/metal/bus/io/io.h>
#include <runtime/time/time.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/IoLib.h>
#include <Library/UefiBootServicesTableLib.h>

#define IDE_SEC           512u
#define IDE_MAX_DRIVES    4u

#define ATA_REG_DATA      0u
#define ATA_REG_ERROR     1u
#define ATA_REG_SECCOUNT  2u
#define ATA_REG_LBA_LO    3u
#define ATA_REG_LBA_MID   4u
#define ATA_REG_LBA_HI    5u
#define ATA_REG_DRIVE     6u
#define ATA_REG_STATUS    7u
#define ATA_REG_CMD       7u

#define ATA_SR_ERR        0x01u
#define ATA_SR_DRQ        0x08u
#define ATA_SR_DF         0x20u
#define ATA_SR_DRDY       0x40u
#define ATA_SR_BSY        0x80u

#define ATA_CMD_IDENTIFY  0xECu
#define ATA_CMD_READ_PIO  0x20u
#define ATA_CMD_WRITE_PIO 0x30u

typedef struct {
  UINT16   CmdBase;
  UINT16   CtrlBase;
  UINT8    Drive; /* 0 master, 1 slave */
  INT32    Ready;
  UINT64   Capacity;
  UINT16   Identify[256];
} ide_drive_t;

STATIC ide_drive_t  mDrives[IDE_MAX_DRIVES];
STATIC UINT32       mDriveCount;

STATIC
VOID
IdeDelay (
  UINT32  us
  )
{
  if (gBS != NULL) {
    gBS->Stall (us);
  } else {
    UINT64  deadline;

    deadline = pm_metal_time_mono_us () + (UINT64)us;
    while (pm_metal_time_mono_us () < deadline) {
    }
  }
}

STATIC
UINT8
IdeStatus (
  CONST ide_drive_t  *d
  )
{
  return IoRead8 ((UINTN)(d->CmdBase + ATA_REG_STATUS));
}

STATIC
INT32
IdeWaitNotBusy (
  CONST ide_drive_t  *d,
  UINT32              timeout_ms
  )
{
  UINT64  deadline;

  deadline = pm_metal_time_mono_us () + (UINT64)timeout_ms * 1000ull;
  while (pm_metal_time_mono_us () < deadline) {
    UINT8  st;

    st = IdeStatus (d);
    if (st == 0xff) {
      return -1;
    }

    if ((st & ATA_SR_BSY) == 0) {
      return 0;
    }

    CpuPause ();
  }

  return -1;
}

STATIC
INT32
IdeWaitDrq (
  CONST ide_drive_t  *d,
  UINT32              timeout_ms
  )
{
  UINT64  deadline;

  deadline = pm_metal_time_mono_us () + (UINT64)timeout_ms * 1000ull;
  while (pm_metal_time_mono_us () < deadline) {
    UINT8  st;

    st = IdeStatus (d);
    if (st == 0xff) {
      return -1;
    }

    if ((st & ATA_SR_BSY) != 0) {
      CpuPause ();
      continue;
    }

    if ((st & (ATA_SR_ERR | ATA_SR_DF)) != 0) {
      return -1;
    }

    if ((st & ATA_SR_DRQ) != 0) {
      return 0;
    }

    CpuPause ();
  }

  return -1;
}

STATIC
VOID
IdeSelect (
  CONST ide_drive_t  *d
  )
{
  IoWrite8 (
    (UINTN)(d->CmdBase + ATA_REG_DRIVE),
    (UINT8)(0xA0u | ((UINT8)(d->Drive & 1u) << 4))
    );
  IdeDelay (400);
}

STATIC
INT32
IdeIdentify (
  ide_drive_t  *d
  )
{
  UINT32  i;
  UINT8   st;

  IdeSelect (d);
  st = IdeStatus (d);
  if (st == 0xff || st == 0x00) {
    return -1;
  }

  IoWrite8 ((UINTN)(d->CmdBase + ATA_REG_SECCOUNT), 0);
  IoWrite8 ((UINTN)(d->CmdBase + ATA_REG_LBA_LO), 0);
  IoWrite8 ((UINTN)(d->CmdBase + ATA_REG_LBA_MID), 0);
  IoWrite8 ((UINTN)(d->CmdBase + ATA_REG_LBA_HI), 0);
  IoWrite8 ((UINTN)(d->CmdBase + ATA_REG_CMD), ATA_CMD_IDENTIFY);

  IdeDelay (400);
  st = IdeStatus (d);
  if (st == 0x00 || st == 0xff) {
    return -1;
  }

  if (IdeWaitNotBusy (d, 2000) != 0) {
    return -1;
  }

  /* ATAPI: mid/hi non-zero after IDENTIFY */
  if (IoRead8 ((UINTN)(d->CmdBase + ATA_REG_LBA_MID)) != 0
      || IoRead8 ((UINTN)(d->CmdBase + ATA_REG_LBA_HI)) != 0)
  {
    return -1;
  }

  if (IdeWaitDrq (d, 2000) != 0) {
    return -1;
  }

  for (i = 0; i < 256; i++) {
    d->Identify[i] = IoRead16 ((UINTN)(d->CmdBase + ATA_REG_DATA));
  }

  d->Capacity = (UINT64)d->Identify[60] | ((UINT64)d->Identify[61] << 16);
  if (d->Capacity == 0) {
    return -1;
  }

  d->Ready = 1;
  return 0;
}

typedef struct {
  UINT32   write;
  UINT32   nsec;
  UINT32   sec;
  UINT16  *words;
  UINT8    phase; /* 0 issued; 1 sector PIO; 2 final !BSY */
} ide_xfer_cookie_t;

STATIC
INT32
IdeXferStart (
  VOID      *ctx,
  INT32      write,
  UINT64     lba,
  VOID      *buf,
  UINT32     nsec,
  VOID      *cookie
  )
{
  ide_drive_t         *d;
  ide_xfer_cookie_t   *c;
  UINT8                st;

  d = (ide_drive_t *)ctx;
  c = (ide_xfer_cookie_t *)cookie;
  if (d == NULL || c == NULL || !d->Ready || buf == NULL || nsec == 0
      || nsec > 256 || lba + nsec > d->Capacity || (lba >> 28) != 0)
  {
    return -1;
  }

  c->write = write ? 1u : 0u;
  c->nsec  = nsec;
  c->sec   = 0;
  c->words = (UINT16 *)buf;
  c->phase = 0;

  IdeSelect (d);
  st = IdeStatus (d);
  if (st == 0xff || (st & ATA_SR_BSY) != 0) {
    return -1;
  }

  IoWrite8 ((UINTN)(d->CmdBase + ATA_REG_SECCOUNT), (UINT8)nsec);
  IoWrite8 ((UINTN)(d->CmdBase + ATA_REG_LBA_LO), (UINT8)(lba & 0xffu));
  IoWrite8 ((UINTN)(d->CmdBase + ATA_REG_LBA_MID), (UINT8)((lba >> 8) & 0xffu));
  IoWrite8 ((UINTN)(d->CmdBase + ATA_REG_LBA_HI), (UINT8)((lba >> 16) & 0xffu));
  IoWrite8 (
    (UINTN)(d->CmdBase + ATA_REG_DRIVE),
    (UINT8)(0xE0u | ((UINT8)(d->Drive & 1u) << 4) | (UINT8)((lba >> 24) & 0x0fu))
    );
  IoWrite8 (
    (UINTN)(d->CmdBase + ATA_REG_CMD),
    write ? ATA_CMD_WRITE_PIO : ATA_CMD_READ_PIO
    );
  c->phase = 1;
  return 0;
}

STATIC
INT32
IdeXferPoll (
  VOID  *ctx,
  VOID  *cookie
  )
{
  ide_drive_t        *d;
  ide_xfer_cookie_t  *c;
  UINT8               st;
  UINT32              i;

  d = (ide_drive_t *)ctx;
  c = (ide_xfer_cookie_t *)cookie;
  if (d == NULL || c == NULL) {
    return -1;
  }

  st = IdeStatus (d);
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
        IoWrite16 (
          (UINTN)(d->CmdBase + ATA_REG_DATA),
          c->words[c->sec * 256u + i]
          );
      }
    } else {
      for (i = 0; i < 256; i++) {
        c->words[c->sec * 256u + i] = IoRead16 (
                                        (UINTN)(d->CmdBase + ATA_REG_DATA)
                                        );
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

STATIC
INT32
IdeXferFinish (
  VOID  *ctx,
  VOID  *cookie
  )
{
  (VOID)ctx;
  (VOID)cookie;
  return 0;
}

STATIC
INT32
IdePioXfer (
  ide_drive_t  *d,
  UINT32        Write,
  UINT64        Lba,
  VOID         *Buf,
  UINT32        Nsec
  )
{
  ide_xfer_cookie_t  cookie;
  UINT64             deadline;

  /* Sync path for IDENTIFY-era / boot: poll with CpuPause, no Stall. */
  if (d == NULL || !d->Ready) {
    return -1;
  }

  IdeSelect (d);
  if (IdeWaitNotBusy (d, 2000) != 0) {
    return -1;
  }

  ZeroMem (&cookie, sizeof (cookie));
  if (IdeXferStart (d, (INT32)Write, Lba, Buf, Nsec, &cookie) != 0) {
    return -1;
  }

  deadline = pm_metal_time_mono_us () + 5000000ull;
  while (pm_metal_time_mono_us () < deadline) {
    INT32  r;

    r = IdeXferPoll (d, &cookie);
    if (r < 0) {
      return -1;
    }

    if (r > 0) {
      return IdeXferFinish (d, &cookie);
    }

    CpuPause ();
  }

  return -1;
}

STATIC
int
IdeReady (
  VOID  *ctx
  )
{
  ide_drive_t  *d;

  d = (ide_drive_t *)ctx;
  return (d != NULL && d->Ready) ? 1 : 0;
}

STATIC
uint64_t
IdeCapacity (
  VOID  *ctx
  )
{
  ide_drive_t  *d;

  d = (ide_drive_t *)ctx;
  return (d != NULL) ? d->Capacity : 0;
}

STATIC
int
IdeRead (
  VOID      *ctx,
  uint64_t   lba,
  VOID      *buf,
  uint32_t   nsec
  )
{
  return IdePioXfer ((ide_drive_t *)ctx, 0, lba, buf, nsec);
}

STATIC
int
IdeWrite (
  VOID        *ctx,
  uint64_t     lba,
  CONST VOID  *buf,
  uint32_t     nsec
  )
{
  return IdePioXfer ((ide_drive_t *)ctx, 1, lba, (VOID *)buf, nsec);
}

STATIC
INT32
IdeBindDrive (
  ide_drive_t  *d
  )
{
  pm_metal_io_node_t  Node;
  pm_metal_blk_ops_t  Ops;
  INT32               dt_id;

  ZeroMem (&Node, sizeof (Node));
  Node.class  = PM_METAL_IO_BLK;
  Node.compat = "ide-ata";
  Node.caps   = 1;
  Node.bus    = PM_METAL_IO_BUS_ISA;
  Node.loc[0] = d->CmdBase;
  Node.loc[1] = d->CtrlBase;
  Node.loc[2] = d->Drive;
  Node.loc[3] = 0;
  dt_id       = pm_metal_io_dt_add (&Node);
  if (dt_id < 0) {
    return -1;
  }

  ZeroMem (&Ops, sizeof (Ops));
  Ops.compat      = "ide-ata";
  Ops.dt_id       = (UINT32)dt_id;
  Ops.ready       = IdeReady;
  Ops.capacity    = IdeCapacity;
  Ops.read        = IdeRead;
  Ops.write       = IdeWrite;
  Ops.xfer_start  = IdeXferStart;
  Ops.xfer_poll   = IdeXferPoll;
  Ops.xfer_finish = IdeXferFinish;
  Ops.ctx         = d;
  if (pm_metal_blk_bind (&Ops) == PM_METAL_BLK_INVALID) {
    return -1;
  }

  return 0;
}

STATIC
VOID
IdeProbeChannel (
  UINT16  CmdBase,
  UINT16  CtrlBase
  )
{
  UINT8  drive;

  for (drive = 0; drive < 2; drive++) {
    ide_drive_t  *d;

    if (mDriveCount >= IDE_MAX_DRIVES) {
      return;
    }

    d = &mDrives[mDriveCount];
    ZeroMem (d, sizeof (*d));
    d->CmdBase  = CmdBase;
    d->CtrlBase = CtrlBase;
    d->Drive    = drive;
    if (IdeIdentify (d) != 0) {
      continue;
    }

    if (IdeBindDrive (d) != 0) {
      d->Ready = 0;
      continue;
    }

    mDriveCount++;
  }
}

int
pm_metal_blk_ide_detect (
  VOID
  )
{
  /* Legacy ISA command blocks — present on ICH4-M / PIIX / QEMU IDE. */
  IdeProbeChannel (0x1F0, 0x3F6);
  IdeProbeChannel (0x170, 0x376);
  return (mDriveCount > 0) ? 0 : -1;
}
