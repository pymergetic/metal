/** @file
  Unified log — ring buffer + viewports. (impl: efi|bios)
**/
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/dev/console/console.h>
#include <pymergetic/metal/shell/ui/ui.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/PrintLib.h>
#include <Library/SynchronizationLib.h>
#include <Library/UefiLib.h>

#define PM_METAL_LOG_LINES  512u
#define PM_METAL_LOG_COLS   160u

typedef struct {
  UINT32  marker;     /* next line index (absolute generation) to drain */
  UINT8   on_buffer;  /* counts toward viewport_count */
  UINT8   direct;     /* receive new lines without buffer */
  UINT8   live;       /* emit immediately while on_buffer (UEFI) */
  UINT8   open;
} metal_log_vp_t;

STATIC CHAR8          mLines[PM_METAL_LOG_LINES][PM_METAL_LOG_COLS];
STATIC UINT32         mGen;          /* absolute lines appended */
STATIC UINT32         mViewportCount;
STATIC UINT32         mUartResumeGen; /* marker remembered at EBS */
STATIC UINT8          mUartResumeValid;
STATIC UINT8          mBootEpoch;     /* retain ring until boot_complete */
STATIC UINT8          mInited;
STATIC SPIN_LOCK      mLock;
STATIC metal_log_vp_t mVp[PM_METAL_LOG_VP_COUNT];

STATIC
VOID
LogEmitUefi (
  CONST CHAR8  *line
  )
{
  if (line == NULL) {
    return;
  }

  /* ConOut only — OVMF mirrors to serial; COM1 is the post-EBS UART viewport. */
  Print (L"%a\r\n", line);
}

STATIC
VOID
LogEmitUart (
  CONST CHAR8  *line
  )
{
  UINTN  n;

  if (line == NULL) {
    return;
  }

  n = AsciiStrLen (line);
  if (n > 0) {
    pm_metal_console_com1_write (line, (UINT32)n);
  }

  pm_metal_console_com1_write ("\r\n", 2);
  if (pm_metal_console_ready ()) {
    if (n > 0) {
      (VOID)pm_metal_console_write (line, (UINT32)n);
    }

    (VOID)pm_metal_console_write ("\r\n", 2);
  }
}

STATIC
VOID
LogEmitUi (
  CONST CHAR8  *line
  )
{
  if (line == NULL) {
    return;
  }

  pm_metal_ui_console_puts (line);
}

STATIC
VOID
LogEmitVp (
  pm_metal_log_vp_t  id,
  CONST CHAR8       *line
  )
{
  switch (id) {
    case PM_METAL_LOG_VP_UEFI:
      LogEmitUefi (line);
      break;
    case PM_METAL_LOG_VP_UART:
      LogEmitUart (line);
      break;
    case PM_METAL_LOG_VP_UI:
      LogEmitUi (line);
      break;
    default:
      break;
  }
}

STATIC
CONST CHAR8 *
LogLineAt (
  UINT32  gen
  )
{
  return mLines[gen % PM_METAL_LOG_LINES];
}

STATIC
VOID
LogDrainVp (
  pm_metal_log_vp_t  id,
  UINT32             from_gen,
  UINT32             to_gen
  )
{
  UINT32  g;

  for (g = from_gen; g < to_gen; g++) {
    /* Drop lines overwritten in the ring. */
    if (mGen > PM_METAL_LOG_LINES && g < mGen - PM_METAL_LOG_LINES) {
      continue;
    }

    LogEmitVp (id, LogLineAt (g));
  }
}

STATIC
VOID
LogTryClearRing (
  VOID
  )
{
  /* Ring stays for the boot epoch so UART/UI can still drain. */
  if (mBootEpoch) {
    return;
  }

  if (mViewportCount == 0) {
    ZeroMem (mLines, sizeof (mLines));
    mGen = 0;
  }
}

STATIC
VOID
LogDetachBuffer (
  pm_metal_log_vp_t  id
  )
{
  metal_log_vp_t  *vp;

  vp = &mVp[id];
  if (!vp->on_buffer) {
    return;
  }

  vp->on_buffer = 0;
  if (mViewportCount > 0) {
    mViewportCount--;
  }

  LogTryClearRing ();
}

void
pm_metal_log_init (
  VOID
  )
{
  if (mInited) {
    return;
  }

  InitializeSpinLock (&mLock);
  ZeroMem (mLines, sizeof (mLines));
  ZeroMem (mVp, sizeof (mVp));
  mGen             = 0;
  mViewportCount   = 0;
  mUartResumeGen   = 0;
  mUartResumeValid = 0;
  mBootEpoch       = 1;

  mVp[PM_METAL_LOG_VP_UEFI].open      = 1;
  mVp[PM_METAL_LOG_VP_UEFI].on_buffer = 1;
  mVp[PM_METAL_LOG_VP_UEFI].live      = 1;
  mVp[PM_METAL_LOG_VP_UEFI].marker    = 0;
  mViewportCount = 1;
  mInited        = 1;
}

void
pm_metal_log (
  CONST CHAR8  *line
  )
{
  UINT32  i;
  UINT32  slot;
  UINTN   n;

  if (line == NULL) {
    return;
  }

  if (!mInited) {
    pm_metal_log_init ();
  }

  AcquireSpinLock (&mLock);

  if (mBootEpoch || mViewportCount > 0) {
    slot = mGen % PM_METAL_LOG_LINES;
    n    = AsciiStrnLenS (line, PM_METAL_LOG_COLS - 1);
    CopyMem (mLines[slot], line, n);
    mLines[slot][n] = '\0';
    mGen++;

    for (i = 0; i < (UINT32)PM_METAL_LOG_VP_COUNT; i++) {
      if (mVp[i].on_buffer && mVp[i].live) {
        LogEmitVp ((pm_metal_log_vp_t)i, line);
        mVp[i].marker = mGen;
      }
    }
  }

  for (i = 0; i < (UINT32)PM_METAL_LOG_VP_COUNT; i++) {
    if (mVp[i].direct) {
      LogEmitVp ((pm_metal_log_vp_t)i, line);
    }
  }

  ReleaseSpinLock (&mLock);
}

VOID
EFIAPI
pm_metal_logf (
  IN CONST CHAR8  *fmt,
  ...
  )
{
  VA_LIST  args;
  CHAR8    buf[PM_METAL_LOG_COLS];

  if (fmt == NULL) {
    return;
  }

  /* Must be EFIAPI: AsciiVSPrint expects ms_abi VA_LIST on X64. */
  VA_START (args, fmt);
  AsciiVSPrint (buf, sizeof (buf), fmt, args);
  VA_END (args);
  pm_metal_log (buf);
}

void
pm_metal_log_ebs_close_uefi (
  VOID
  )
{
  if (!mInited) {
    return;
  }

  AcquireSpinLock (&mLock);
  mUartResumeGen   = mVp[PM_METAL_LOG_VP_UEFI].marker;
  mUartResumeValid = 1;
  mVp[PM_METAL_LOG_VP_UEFI].open = 0;
  mVp[PM_METAL_LOG_VP_UEFI].live = 0;
  LogDetachBuffer (PM_METAL_LOG_VP_UEFI);
  ReleaseSpinLock (&mLock);
}

void
pm_metal_log_attach_uart (
  VOID
  )
{
  UINT32  from;
  UINT32  to;

  if (!mInited) {
    pm_metal_log_init ();
  }

  AcquireSpinLock (&mLock);
  if (mVp[PM_METAL_LOG_VP_UART].direct) {
    ReleaseSpinLock (&mLock);
    return;
  }

  from = mUartResumeValid ? mUartResumeGen : 0;
  to   = mGen;
  LogDrainVp (PM_METAL_LOG_VP_UART, from, to);
  mVp[PM_METAL_LOG_VP_UART].open   = 1;
  mVp[PM_METAL_LOG_VP_UART].direct = 1;
  mVp[PM_METAL_LOG_VP_UART].live   = 0;
  if (mVp[PM_METAL_LOG_VP_UART].on_buffer) {
    LogDetachBuffer (PM_METAL_LOG_VP_UART);
  }

  ReleaseSpinLock (&mLock);
}

void
pm_metal_log_attach_ui (
  VOID
  )
{
  UINT32  to;

  if (!mInited) {
    pm_metal_log_init ();
  }

  AcquireSpinLock (&mLock);
  to = mGen;
  /* Full history from the oldest retained line. */
  LogDrainVp (
    PM_METAL_LOG_VP_UI,
    (mGen > PM_METAL_LOG_LINES) ? (mGen - PM_METAL_LOG_LINES) : 0,
    to
    );
  mVp[PM_METAL_LOG_VP_UI].open   = 1;
  mVp[PM_METAL_LOG_VP_UI].direct = 1;
  mVp[PM_METAL_LOG_VP_UI].live   = 0;
  if (mVp[PM_METAL_LOG_VP_UI].on_buffer) {
    LogDetachBuffer (PM_METAL_LOG_VP_UI);
  }

  ReleaseSpinLock (&mLock);
}

int
pm_metal_log_buffer_live (
  VOID
  )
{
  return (mBootEpoch || mViewportCount > 0) ? 1 : 0;
}

void
pm_metal_log_boot_complete (
  VOID
  )
{
  if (!mInited) {
    return;
  }

  AcquireSpinLock (&mLock);
  mBootEpoch = 0;
  LogTryClearRing ();
  ReleaseSpinLock (&mLock);
}
