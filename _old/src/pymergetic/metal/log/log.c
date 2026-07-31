/** @file
  Unified log — ring buffer + viewports + semantic styles. (impl: efi|bios)
**/
#include <pymergetic/metal/log/log.h>
#include <pymergetic/metal/dev/console/console.h>
#include <pymergetic/metal/shell/ui/ui.h>
#include <runtime/slot/spin.h>

#include <stdarg.h>
#include <string.h>

/* impl: efi|bios — src/{efi,bios}/pymergetic/metal/log/log_port.c
 * the one EDK2 (UefiLib Print) touchpoint this file needs. */
void pm_metal_log_port_emit_uefi(const char *line);

/* Portable vsnprintf (runtime/mem/libc.c) — no EDK2 AsciiVSPrint needed. */
int vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);

#define PM_METAL_LOG_LINES 512u
/*
 * 256: metal-perf diagnostic lines (cpu/frame/step/gap/present max + the
 * present_cpu/offloads fields) run to ~210 chars — the old 160 silently
 * truncated pm_metal_logf() calls outside the UART/serial fast path.
 */
#define PM_METAL_LOG_COLS 256u

typedef struct {
  uint32_t marker;    /* next line index (absolute generation) to drain */
  uint8_t  on_buffer; /* counts toward viewport_count */
  uint8_t  direct;    /* receive new lines without buffer */
  uint8_t  live;      /* emit immediately while on_buffer (UEFI) */
  uint8_t  open;
} metal_log_vp_t;

static char            mLines[PM_METAL_LOG_LINES][PM_METAL_LOG_COLS];
static uint8_t         mStyles[PM_METAL_LOG_LINES];
static uint32_t        mGen; /* absolute lines appended */
static uint32_t        mViewportCount;
static uint32_t        mUartResumeGen; /* marker remembered at EBS */
static uint8_t         mUartResumeValid;
static uint8_t         mBootEpoch; /* retain ring until boot_complete */
static uint8_t         mInited;
static pm_metal_spin_t mLock;
static metal_log_vp_t  mVp[PM_METAL_LOG_VP_COUNT];

static const char *LogAnsiPrefix(pm_metal_log_style_t style)
{
  switch (style) {
  case PM_METAL_LOG_STYLE_DIM:
    return "\033[2m";
  case PM_METAL_LOG_STYLE_OK:
    return "\033[32m";
  case PM_METAL_LOG_STYLE_WARN:
    return "\033[33m";
  case PM_METAL_LOG_STYLE_FAIL:
    return "\033[31m";
  case PM_METAL_LOG_STYLE_ACCENT:
    return "\033[36m";
  case PM_METAL_LOG_STYLE_DEFAULT:
  default:
    return NULL;
  }
}

static void LogEmitUefi(const char *line, pm_metal_log_style_t style)
{
  (void)style;
  if (line == NULL) {
    return;
  }

  /* ConOut only — plain text (no attribute API). */
  pm_metal_log_port_emit_uefi(line);
}

static void LogEmitUart(const char *line, pm_metal_log_style_t style)
{
  size_t      n;
  const char *pre;

  if (line == NULL) {
    return;
  }

  n   = strlen(line);
  pre = (n > 0) ? LogAnsiPrefix(style) : NULL;

  if (pre != NULL) {
    pm_metal_console_com1_write(pre, (uint32_t)strlen(pre));
  }

  if (n > 0) {
    pm_metal_console_com1_write(line, (uint32_t)n);
  }

  if (pre != NULL) {
    pm_metal_console_com1_write("\033[0m", 4);
  }

  pm_metal_console_com1_write("\r\n", 2);
  if (pm_metal_console_ready()) {
    if (pre != NULL) {
      (void)pm_metal_console_write(pre, (uint32_t)strlen(pre));
    }

    if (n > 0) {
      (void)pm_metal_console_write(line, (uint32_t)n);
    }

    if (pre != NULL) {
      (void)pm_metal_console_write("\033[0m", 4);
    }

    (void)pm_metal_console_write("\r\n", 2);
  }
}

static void LogEmitUi(const char *line, pm_metal_log_style_t style)
{
  if (line == NULL) {
    return;
  }

  pm_metal_ui_console_puts_styled(style, line);
}

static void LogEmitVp(pm_metal_log_vp_t id, const char *line, pm_metal_log_style_t style)
{
  switch (id) {
  case PM_METAL_LOG_VP_UEFI:
    LogEmitUefi(line, style);
    break;
  case PM_METAL_LOG_VP_UART:
    LogEmitUart(line, style);
    break;
  case PM_METAL_LOG_VP_UI:
    LogEmitUi(line, style);
    break;
  default:
    break;
  }
}

static const char *LogLineAt(uint32_t gen)
{
  return mLines[gen % PM_METAL_LOG_LINES];
}

static pm_metal_log_style_t LogStyleAt(uint32_t gen)
{
  return (pm_metal_log_style_t)mStyles[gen % PM_METAL_LOG_LINES];
}

static void LogDrainVp(pm_metal_log_vp_t id, uint32_t from_gen, uint32_t to_gen)
{
  uint32_t g;

  for (g = from_gen; g < to_gen; g++) {
    /* Drop lines overwritten in the ring. */
    if (mGen > PM_METAL_LOG_LINES && g < mGen - PM_METAL_LOG_LINES) {
      continue;
    }

    LogEmitVp(id, LogLineAt(g), LogStyleAt(g));
  }
}

static void LogTryClearRing(void)
{
  /* Ring stays for the boot epoch so UART/UI can still drain. */
  if (mBootEpoch) {
    return;
  }

  if (mViewportCount == 0) {
    memset(mLines, 0, sizeof(mLines));
    memset(mStyles, 0, sizeof(mStyles));
    mGen = 0;
  }
}

static void LogDetachBuffer(pm_metal_log_vp_t id)
{
  metal_log_vp_t *vp;

  vp = &mVp[id];
  if (!vp->on_buffer) {
    return;
  }

  vp->on_buffer = 0;
  if (mViewportCount > 0) {
    mViewportCount--;
  }

  LogTryClearRing();
}

void pm_metal_log_init(void)
{
  if (mInited) {
    return;
  }

  pm_metal_spin_init(&mLock);
  memset(mLines, 0, sizeof(mLines));
  memset(mStyles, 0, sizeof(mStyles));
  memset(mVp, 0, sizeof(mVp));
  mGen             = 0;
  mViewportCount   = 0;
  mUartResumeGen   = 0;
  mUartResumeValid = 0;
  mBootEpoch       = 1;

  mVp[PM_METAL_LOG_VP_UEFI].open      = 1;
  mVp[PM_METAL_LOG_VP_UEFI].on_buffer = 1;
  mVp[PM_METAL_LOG_VP_UEFI].live      = 1;
  mVp[PM_METAL_LOG_VP_UEFI].marker    = 0;
  mViewportCount                      = 1;
  mInited                             = 1;
}

void pm_metal_log_styled(pm_metal_log_style_t style, const char *line)
{
  uint32_t i;
  uint32_t slot;
  size_t   n;

  if (line == NULL) {
    return;
  }

  if (!mInited) {
    pm_metal_log_init();
  }

  pm_metal_spin_lock(&mLock);

  if (mBootEpoch || mViewportCount > 0) {
    slot = mGen % PM_METAL_LOG_LINES;
    n    = strlen(line);
    if (n > PM_METAL_LOG_COLS - 1) {
      n = PM_METAL_LOG_COLS - 1;
    }
    memcpy(mLines[slot], line, n);
    mLines[slot][n] = '\0';
    mStyles[slot]   = (uint8_t)style;
    mGen++;

    for (i = 0; i < (uint32_t)PM_METAL_LOG_VP_COUNT; i++) {
      if (mVp[i].on_buffer && mVp[i].live) {
        LogEmitVp((pm_metal_log_vp_t)i, line, style);
        mVp[i].marker = mGen;
      }
    }
  }

  for (i = 0; i < (uint32_t)PM_METAL_LOG_VP_COUNT; i++) {
    if (mVp[i].direct) {
      LogEmitVp((pm_metal_log_vp_t)i, line, style);
    }
  }

  pm_metal_spin_unlock(&mLock);
}

void pm_metal_log(const char *line)
{
  pm_metal_log_styled(PM_METAL_LOG_STYLE_DEFAULT, line);
}

void pm_metal_logf_styled(pm_metal_log_style_t style, const char *fmt, ...)
{
  va_list args;
  char    buf[PM_METAL_LOG_COLS];

  if (fmt == NULL) {
    return;
  }

  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  pm_metal_log_styled(style, buf);
}

void pm_metal_logf(const char *fmt, ...)
{
  va_list args;
  char    buf[PM_METAL_LOG_COLS];

  if (fmt == NULL) {
    return;
  }

  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  pm_metal_log_styled(PM_METAL_LOG_STYLE_DEFAULT, buf);
}

void pm_metal_log_ebs_close_uefi(void)
{
  if (!mInited) {
    return;
  }

  pm_metal_spin_lock(&mLock);
  mUartResumeGen                 = mVp[PM_METAL_LOG_VP_UEFI].marker;
  mUartResumeValid               = 1;
  mVp[PM_METAL_LOG_VP_UEFI].open = 0;
  mVp[PM_METAL_LOG_VP_UEFI].live = 0;
  LogDetachBuffer(PM_METAL_LOG_VP_UEFI);
  pm_metal_spin_unlock(&mLock);
}

void pm_metal_log_attach_uart(void)
{
  uint32_t from;
  uint32_t to;

  if (!mInited) {
    pm_metal_log_init();
  }

  pm_metal_spin_lock(&mLock);
  if (mVp[PM_METAL_LOG_VP_UART].direct) {
    pm_metal_spin_unlock(&mLock);
    return;
  }

  from = mUartResumeValid ? mUartResumeGen : 0;
  to   = mGen;
  LogDrainVp(PM_METAL_LOG_VP_UART, from, to);
  mVp[PM_METAL_LOG_VP_UART].open   = 1;
  mVp[PM_METAL_LOG_VP_UART].direct = 1;
  mVp[PM_METAL_LOG_VP_UART].live   = 0;
  if (mVp[PM_METAL_LOG_VP_UART].on_buffer) {
    LogDetachBuffer(PM_METAL_LOG_VP_UART);
  }

  pm_metal_spin_unlock(&mLock);
}

void pm_metal_log_attach_ui(void)
{
  uint32_t to;

  if (!mInited) {
    pm_metal_log_init();
  }

  pm_metal_spin_lock(&mLock);
  to = mGen;
  /* Full history from the oldest retained line. */
  LogDrainVp(PM_METAL_LOG_VP_UI, (mGen > PM_METAL_LOG_LINES) ? (mGen - PM_METAL_LOG_LINES) : 0, to);
  mVp[PM_METAL_LOG_VP_UI].open   = 1;
  mVp[PM_METAL_LOG_VP_UI].direct = 1;
  mVp[PM_METAL_LOG_VP_UI].live   = 0;
  if (mVp[PM_METAL_LOG_VP_UI].on_buffer) {
    LogDetachBuffer(PM_METAL_LOG_VP_UI);
  }

  pm_metal_spin_unlock(&mLock);
}

int pm_metal_log_buffer_live(void)
{
  return (mBootEpoch || mViewportCount > 0) ? 1 : 0;
}

void pm_metal_log_boot_complete(void)
{
  if (!mInited) {
    return;
  }

  pm_metal_spin_lock(&mLock);
  mBootEpoch = 0;
  LogTryClearRing();
  pm_metal_spin_unlock(&mLock);
}
