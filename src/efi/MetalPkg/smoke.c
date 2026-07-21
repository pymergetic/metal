/** @file
  Runloop + asyncio load smoke. CPU-agnostic: create_task round-robins;
  all loopers stay up until stop_on_done. No spawn(0) / mem_set_cpu.
**/
#include "smoke.h"

#include <Uefi.h>
#include <Pi/PiMultiPhase.h>
#include <Protocol/MpService.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/CpuLib.h>

#include <coro/coro.h>
#include <task/task.h>
#include <run/run.h>
#include <time/time.h>
#include <mem/mem.h>
#include <pymergetic/metal/log/log.h>

#define METAL_SMOKE_ADD        100u
#define METAL_LOAD_WAVE_WIDTH  8u
#define METAL_LOAD_WAVES       4u
#define METAL_LOAD_WORKERS     (METAL_LOAD_WAVE_WIDTH * METAL_LOAD_WAVES)

STATIC EFI_MP_SERVICES_PROTOCOL  *mMp;
STATIC volatile UINT32            mApsDone;

STATIC
VOID
EFIAPI
MetalApEnter (
  IN OUT VOID  *Buffer
  )
{
  UINTN  Cpu;

  (VOID)Buffer;
  if (mMp == NULL) {
    return;
  }

  Cpu = 0;
  (VOID)mMp->WhoAmI (mMp, &Cpu);
  pm_metal_run_enter ((unsigned)Cpu);
}

STATIC
VOID
EFIAPI
MetalApDoneNotify (
  IN EFI_EVENT  Event,
  IN VOID       *Context
  )
{
  (VOID)Event;
  (VOID)Context;
  mApsDone = 1;
}

typedef struct {
  pm_metal_coro_t  coro;
  UINT32           step;
  UINT32           in;
  UINT32           out;
} add_coro_t;

STATIC
pm_metal_status_t
AddCoroFn (
  pm_metal_coro_t  *self
  )
{
  add_coro_t  *s = (add_coro_t *)self;

  if (s->step == 0) {
    s->step = 1;
    return PM_METAL_WAITING;
  }

  s->out       = s->in + METAL_SMOKE_ADD;
  self->result = &s->out;
  return PM_METAL_DONE;
}

typedef struct {
  pm_metal_coro_t  coro;
  UINT32           step;
  UINT32           id;
  UINT32           sleep_ms;
  INT32           *out_slot;
} worker_coro_t;

STATIC
pm_metal_status_t
WorkerCoroFn (
  pm_metal_coro_t  *self
  )
{
  worker_coro_t  *s = (worker_coro_t *)self;

  if (s->step == 0) {
    s->step = 1;
    return pm_metal_await (self, pm_metal_sleep (s->sleep_ms));
  }

  if (s->out_slot == NULL) {
    return PM_METAL_ERROR;
  }

  *s->out_slot = (INT32)(s->id + 1u);
  self->result = s->out_slot;
  return PM_METAL_DONE;
}

typedef struct {
  pm_metal_coro_t  coro;
  UINT32           step;
  INT32            mark;
} bg_coro_t;

STATIC
pm_metal_status_t
BgCoroFn (
  pm_metal_coro_t  *self
  )
{
  bg_coro_t  *s = (bg_coro_t *)self;

  if (s->step == 0) {
    s->step = 1;
    return pm_metal_await (self, pm_metal_sleep (5));
  }

  s->mark      = 0xB6;
  self->result = &s->mark;
  return PM_METAL_DONE;
}

typedef struct {
  pm_metal_coro_t   coro;
  volatile INT32   *flag;
} yield_peer_coro_t;

STATIC
pm_metal_status_t
YieldPeerFn (
  pm_metal_coro_t  *self
  )
{
  yield_peer_coro_t  *s = (yield_peer_coro_t *)self;

  if (s->flag == NULL) {
    return PM_METAL_ERROR;
  }

  *s->flag = 1;
  return PM_METAL_DONE;
}

typedef enum {
  LOAD_YIELD = 0,
  LOAD_YIELD_CHECK,
  LOAD_WAVE,
  LOAD_AFTER,
  LOAD_JOIN_BG
} load_step_t;

typedef struct {
  pm_metal_coro_t   coro;
  load_step_t       step;
  UINT32            wave;
  INT32             slots[METAL_LOAD_WAVE_WIDTH];
  INT32             total;
  INT32             expect;
  INT32             yield_ok;
  volatile INT32    peer_ran;
  pm_metal_task_t  *bg;
} load_coro_t;

STATIC
pm_metal_coro_t *
LoadBuildWave (
  load_coro_t  *load
  )
{
  pm_metal_coro_t  *kids[METAL_LOAD_WAVE_WIDTH];
  UINT32            k;
  UINT32            base;

  base = load->wave * METAL_LOAD_WAVE_WIDTH;
  for (k = 0; k < METAL_LOAD_WAVE_WIDTH; k++) {
    worker_coro_t  *w;

    w = (worker_coro_t *)pm_metal_coro (WorkerCoroFn, sizeof (*w));
    if (w == NULL) {
      return NULL;
    }

    load->slots[k] = 0;
    w->id          = base + k;
    w->sleep_ms    = 1u + (k & 3u);
    w->out_slot    = &load->slots[k];
    kids[k]        = &w->coro;
  }

  /* gather → create_task each kid → RR across CPUs */
  return pm_metal_gather (kids, METAL_LOAD_WAVE_WIDTH);
}

STATIC
pm_metal_status_t
LoadCoroFn (
  pm_metal_coro_t  *self
  )
{
  load_coro_t       *s = (load_coro_t *)self;
  pm_metal_status_t  st;
  pm_metal_coro_t   *g;
  pm_metal_coro_t   *y;
  UINT32             k;

  switch (s->step) {
    case LOAD_YIELD:
      {
        yield_peer_coro_t  *peer;
        pm_metal_task_t    *pt;

        /*
          Fairness: peer is queued on *this* CPU, then we yield. Inbox FIFO
          must run peer before we resume — proves yield is schedule, not time.
        */
        if (self->owner == NULL) {
          return PM_METAL_ERROR;
        }

        peer = (yield_peer_coro_t *)pm_metal_coro (
                                      YieldPeerFn,
                                      sizeof (*peer)
                                      );
        if (peer == NULL) {
          return PM_METAL_ERROR;
        }

        s->peer_ran = 0;
        peer->flag  = &s->peer_ran;
        pt = pm_metal_task_new (&peer->coro);
        if (pt == NULL) {
          return PM_METAL_ERROR;
        }

        if (pm_metal_task_spawn (pt, self->owner->cpu) != 0) {
          return PM_METAL_ERROR;
        }

        y = pm_metal_yield ();
        if (y == NULL) {
          return PM_METAL_ERROR;
        }

        s->step = LOAD_YIELD_CHECK;
        return pm_metal_await (self, y);
      }

    case LOAD_YIELD_CHECK:
      if (s->peer_ran != 1) {
        return PM_METAL_ERROR;
      }

      s->yield_ok = 1;
      s->step     = LOAD_WAVE;
      /* fall through */

    case LOAD_WAVE:
      g = LoadBuildWave (s);
      if (g == NULL) {
        return PM_METAL_ERROR;
      }

      s->step = LOAD_AFTER;
      return pm_metal_await (self, g);

    case LOAD_AFTER:
      for (k = 0; k < METAL_LOAD_WAVE_WIDTH; k++) {
        s->total += s->slots[k];
      }

      s->wave++;
      if (s->wave < METAL_LOAD_WAVES) {
        s->step = LOAD_WAVE;
        return pm_metal_await (self, pm_metal_sleep (1));
      }

      s->step = LOAD_JOIN_BG;
      /* fall through */

    case LOAD_JOIN_BG:
      st = pm_metal_await_task (self, s->bg);
      if (st == PM_METAL_WAITING) {
        return PM_METAL_WAITING;
      }

      if (st != PM_METAL_DONE || s->total != s->expect) {
        return PM_METAL_ERROR;
      }

      return PM_METAL_DONE;

    default:
      return PM_METAL_ERROR;
  }
}

STATIC
EFI_STATUS
MetalRunAllCpus (
  unsigned  n
  )
{
  EFI_STATUS  Status;
  EFI_EVENT   Done;

  if (n <= 1) {
    pm_metal_run_enter (0);
    return EFI_SUCCESS;
  }

  Status = gBS->LocateProtocol (&gEfiMpServiceProtocolGuid, NULL, (VOID **)&mMp);
  if (EFI_ERROR (Status) || mMp == NULL) {
    pm_metal_log ("metal-run: MP protocol missing");
    return EFI_UNSUPPORTED;
  }

  /*
    EVT_NOTIFY_SIGNAL is notify-only (not WaitForEvent/CheckEvent).
    MpService signals it when every AP returns from MetalApEnter.
  */
  mApsDone = 0;
  Status = gBS->CreateEvent (
                  EVT_NOTIFY_SIGNAL,
                  TPL_CALLBACK,
                  MetalApDoneNotify,
                  NULL,
                  &Done
                  );
  if (EFI_ERROR (Status)) {
    pm_metal_logf ("metal-run: CreateEvent failed: %r", Status);
    return Status;
  }

  Status = mMp->StartupAllAPs (
                  mMp,
                  MetalApEnter,
                  FALSE,
                  Done,
                  0,
                  NULL,
                  NULL
                  );
  if (EFI_ERROR (Status)) {
    gBS->CloseEvent (Done);
    pm_metal_logf ("metal-run: StartupAllAPs failed: %r", Status);
    return Status;
  }

  pm_metal_run_enter (0);

  while (mApsDone == 0) {
    CpuPause ();
  }

  gBS->CloseEvent (Done);

  pm_metal_logf ("metal-run: parallel join ok (%u APs)", n - 1u);
  return EFI_SUCCESS;
}

EFI_STATUS
MetalRunSmoke (
  VOID
  )
{
  EFI_STATUS       Status;
  unsigned         n;
  unsigned         i;
  load_coro_t     *load;
  bg_coro_t       *bg;
  pm_metal_task_t *load_task;

  n = pm_metal_mem_n_cpus ();
  if (n == 0 || pm_metal_run_init (n) != 0) {
    pm_metal_log ("metal-run: init failed");
    return EFI_OUT_OF_RESOURCES;
  }

  pm_metal_logf ("metal-run: %u inbox(es) ready", n);

  pm_metal_time_msleep (1);
  if (pm_metal_time_mono_us () == 0) {
    pm_metal_log ("metal-time: mono_us failed");
    return EFI_DEVICE_ERROR;
  }

  pm_metal_log ("metal-time: ok");

  bg   = (bg_coro_t *)pm_metal_coro (BgCoroFn, sizeof (*bg));
  load = (load_coro_t *)pm_metal_coro (LoadCoroFn, sizeof (*load));
  if (bg == NULL || load == NULL) {
    pm_metal_log ("metal-coro: alloc failed");
    return EFI_OUT_OF_RESOURCES;
  }

  load->expect  = (INT32)((METAL_LOAD_WORKERS * (METAL_LOAD_WORKERS + 1u)) / 2u);
  load->step    = LOAD_YIELD;
  load->bg      = pm_metal_create_task (&bg->coro);
  load_task     = pm_metal_create_task (&load->coro);
  if (load->bg == NULL || load_task == NULL) {
    pm_metal_log ("metal-coro: create_task failed");
    return EFI_OUT_OF_RESOURCES;
  }

  load_task->stop_on_done = 1;

  pm_metal_logf (
    "metal-coro: load %u workers (%ux%u) + bg (RR)",
    METAL_LOAD_WORKERS,
    METAL_LOAD_WAVES,
    METAL_LOAD_WAVE_WIDTH
    );

  /* Migrators: task_new + one spawn (never create_task then spawn — double post). */
  for (i = 0; i < n; i++) {
    add_coro_t       *add;
    pm_metal_task_t  *t;

    add = (add_coro_t *)pm_metal_coro (AddCoroFn, sizeof (*add));
    if (add == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    add->in = i;
    t = pm_metal_task_new (&add->coro);
    if (t == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    if (pm_metal_task_spawn (t, (i + 1u) % n) != 0) {
      return EFI_OUT_OF_RESOURCES;
    }

    if (pm_metal_run_post (i, PM_METAL_RUN_MSG_ADD, METAL_SMOKE_ADD) != 0) {
      return EFI_OUT_OF_RESOURCES;
    }
  }

  Status = MetalRunAllCpus (n);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  pm_metal_log ("metal-run: enter/leave ok");

  if (pm_metal_task_status (load_task) != PM_METAL_DONE) {
    pm_metal_logf (
      "metal-coro: load not DONE (status=%d total=%d expect=%d)",
      (INT32)pm_metal_task_status (load_task),
      load->total,
      load->expect
      );
    return EFI_DEVICE_ERROR;
  }

  if (load->yield_ok != 1) {
    pm_metal_log ("metal-coro: yield fairness failed");
    return EFI_DEVICE_ERROR;
  }

  pm_metal_log ("metal-coro: yield ok");
  pm_metal_logf (
    "metal-coro: load ok (sum=%d workers=%u)",
    load->total,
    METAL_LOAD_WORKERS
    );
  pm_metal_log ("metal-coro: ok");

  if (pm_metal_run_check (n, METAL_SMOKE_ADD) != 0) {
    pm_metal_log ("metal-run: check failed");
    return EFI_DEVICE_ERROR;
  }

  for (i = 0; i < n; i++) {
    pm_metal_logf (
      "metal-run: cpu%u  done=%u  sum=%u",
      i,
      pm_metal_run_done (i),
      pm_metal_run_sum (i)
      );
  }

  pm_metal_log ("metal-task: ok");
  pm_metal_log ("metal-run: ok");
  return EFI_SUCCESS;
}
