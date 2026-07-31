/** @file
  Runloop + asyncio load smoke. CPU-agnostic: create_task round-robins;
  all loopers stay up until stop_on_done. No spawn(0) / mem_set_cpu.

  Fiber authors use the unified async API. gather still takes coro* — wave
  workers are async fibers bridged via pm_metal_async_host_coro.
**/
#include "smoke.h"

#include <Uefi.h>
#include <Pi/PiMultiPhase.h>
#include <Protocol/MpService.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseLib.h>
#include <Library/CpuLib.h>

#include <pymergetic/metal/runtime/async/async.h>
#include "runtime/async/async_host.h"
#include <runtime/coro/coro.h>
#include <runtime/task/task.h>
#include <runtime/run/run.h>
#include <runtime/time/time.h>
#include <pymergetic/metal/runtime/mem/mem.h>
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
  UINT32  step;
  UINT32  in;
} add_state_t;

STATIC
pm_metal_status_t
AddStep (
  pm_metal_async_handle_t  self_h
  )
{
  add_state_t  *s;

  s = (add_state_t *)(UINTN)pm_metal_async_coro_state (
        self_h
        );
  if (s == NULL) {
    return PM_METAL_ERROR;
  }

  if (s->step == 0) {
    s->step = 1;
    return PM_METAL_WAITING;
  }

  pm_metal_async_set_result_u32 (
    self_h,
    s->in + METAL_SMOKE_ADD
    );
  return PM_METAL_DONE;
}

typedef struct {
  UINT32  step;
  UINT32  id;
  UINT32  sleep_ms;
  INT32  *out_slot;
} worker_state_t;

STATIC
pm_metal_status_t
WorkerStep (
  pm_metal_async_handle_t  self_h
  )
{
  worker_state_t  *s;

  s = (worker_state_t *)(UINTN)pm_metal_async_coro_state (
        self_h
        );
  if (s == NULL) {
    return PM_METAL_ERROR;
  }

  if (s->step == 0) {
    s->step = 1;
    return pm_metal_async_await (
             self_h,
             pm_metal_async_sleep (s->sleep_ms)
             );
  }

  if (s->out_slot == NULL) {
    return PM_METAL_ERROR;
  }

  *s->out_slot = (INT32)(s->id + 1u);
  pm_metal_async_set_result_u32 (
    self_h,
    (UINT32)(s->id + 1u)
    );
  return PM_METAL_DONE;
}

typedef struct {
  UINT32  step;
} bg_state_t;

STATIC
pm_metal_status_t
BgStep (
  pm_metal_async_handle_t  self_h
  )
{
  bg_state_t  *s;

  s = (bg_state_t *)(UINTN)pm_metal_async_coro_state (
        self_h
        );
  if (s == NULL) {
    return PM_METAL_ERROR;
  }

  if (s->step == 0) {
    s->step = 1;
    return pm_metal_async_await (
             self_h,
             pm_metal_async_sleep (5)
             );
  }

  pm_metal_async_set_result_u32 (self_h, 0xB6u);
  return PM_METAL_DONE;
}

typedef struct {
  volatile INT32  *flag;
} yield_peer_state_t;

STATIC
pm_metal_status_t
YieldPeerStep (
  pm_metal_async_handle_t  self_h
  )
{
  yield_peer_state_t  *s;

  s = (yield_peer_state_t *)(UINTN)pm_metal_async_coro_state (
        self_h
        );
  if (s == NULL || s->flag == NULL) {
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
  load_step_t              step;
  UINT32                   wave;
  INT32                    slots[METAL_LOAD_WAVE_WIDTH];
  INT32                    total;
  INT32                    expect;
  INT32                    yield_ok;
  volatile INT32           peer_ran;
  pm_metal_async_handle_t  bg;
} load_state_t;

STATIC
pm_metal_async_handle_t
LoadBuildWave (
  load_state_t  *load
  )
{
  pm_metal_coro_t  *kids[METAL_LOAD_WAVE_WIDTH];
  pm_metal_coro_t  *g;
  UINT32            k;
  UINT32            base;

  /*
    gather still takes coro*: author wave kids as async fibers, then bridge
    with host_coro so create_task RR still fans across CPUs.
  */
  base = load->wave * METAL_LOAD_WAVE_WIDTH;
  for (k = 0; k < METAL_LOAD_WAVE_WIDTH; k++) {
    pm_metal_async_handle_t  h;
    worker_state_t          *w;
    pm_metal_coro_t         *c;

    h = pm_metal_async_coro_create (WorkerStep, sizeof (*w));
    if (h == PM_METAL_ASYNC_HANDLE_INVALID) {
      return PM_METAL_ASYNC_HANDLE_INVALID;
    }

    w = (worker_state_t *)(UINTN)pm_metal_async_coro_state (h);
    if (w == NULL) {
      return PM_METAL_ASYNC_HANDLE_INVALID;
    }

    load->slots[k] = 0;
    w->id          = base + k;
    w->sleep_ms    = 1u + (k & 3u);
    w->out_slot    = &load->slots[k];

    c = pm_metal_async_host_coro (h);
    if (c == NULL) {
      return PM_METAL_ASYNC_HANDLE_INVALID;
    }

    kids[k] = c;
  }

  g = pm_metal_gather (kids, METAL_LOAD_WAVE_WIDTH);
  if (g == NULL) {
    return PM_METAL_ASYNC_HANDLE_INVALID;
  }

  return pm_metal_async_adopt_host_coro (g);
}

STATIC
pm_metal_status_t
LoadStep (
  pm_metal_async_handle_t  self_h
  )
{
  load_state_t            *s;
  pm_metal_async_handle_t  self;
  pm_metal_async_handle_t  g_h;
  pm_metal_async_handle_t  y;
  INT32                    st;
  UINT32                   k;

  self = self_h;
  s    = (load_state_t *)(UINTN)pm_metal_async_coro_state (self);
  if (s == NULL) {
    return PM_METAL_ERROR;
  }

  switch (s->step) {
    case LOAD_YIELD:
      {
        pm_metal_async_handle_t  peer_h;
        yield_peer_state_t      *peer;
        pm_metal_coro_t         *self_c;
        pm_metal_coro_t         *peer_c;
        pm_metal_task_t         *pt;

        /*
          Fairness: peer is queued on *this* CPU, then we yield. Inbox FIFO
          must run peer before we resume — proves yield is schedule, not time.
        */
        self_c = pm_metal_async_host_coro (self);
        if (self_c == NULL || self_c->owner == NULL) {
          return PM_METAL_ERROR;
        }

        peer_h = pm_metal_async_coro_create (
                   YieldPeerStep,
                   sizeof (*peer)
                   );
        if (peer_h == PM_METAL_ASYNC_HANDLE_INVALID) {
          return PM_METAL_ERROR;
        }

        peer = (yield_peer_state_t *)(UINTN)pm_metal_async_coro_state (peer_h);
        if (peer == NULL) {
          return PM_METAL_ERROR;
        }

        s->peer_ran = 0;
        peer->flag  = &s->peer_ran;

        peer_c = pm_metal_async_host_coro (peer_h);
        if (peer_c == NULL) {
          return PM_METAL_ERROR;
        }

        pt = pm_metal_task_new (peer_c);
        if (pt == NULL) {
          return PM_METAL_ERROR;
        }

        if (pm_metal_task_spawn (pt, self_c->owner->cpu) != 0) {
          return PM_METAL_ERROR;
        }

        y = pm_metal_async_yield ();
        if (y == PM_METAL_ASYNC_HANDLE_INVALID) {
          return PM_METAL_ERROR;
        }

        s->step = LOAD_YIELD_CHECK;
        return pm_metal_async_await (self, y);
      }

    case LOAD_YIELD_CHECK:
      if (s->peer_ran != 1) {
        return PM_METAL_ERROR;
      }

      s->yield_ok = 1;
      s->step     = LOAD_WAVE;
      /* fall through */

    case LOAD_WAVE:
      g_h = LoadBuildWave (s);
      if (g_h == PM_METAL_ASYNC_HANDLE_INVALID) {
        return PM_METAL_ERROR;
      }

      s->step = LOAD_AFTER;
      return pm_metal_async_await (self, g_h);

    case LOAD_AFTER:
      for (k = 0; k < METAL_LOAD_WAVE_WIDTH; k++) {
        s->total += s->slots[k];
      }

      s->wave++;
      if (s->wave < METAL_LOAD_WAVES) {
        s->step = LOAD_WAVE;
        return pm_metal_async_await (self, pm_metal_async_sleep (1));
      }

      s->step = LOAD_JOIN_BG;
      /* fall through */

    case LOAD_JOIN_BG:
      st = pm_metal_async_await_task (self, s->bg);
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
  EFI_STATUS               Status;
  unsigned                 n;
  unsigned                 i;
  pm_metal_async_handle_t  bg_h;
  pm_metal_async_handle_t  load_h;
  pm_metal_async_handle_t  load_task;
  load_state_t            *load;

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

  bg_h   = pm_metal_async_coro_create (BgStep, sizeof (bg_state_t));
  load_h = pm_metal_async_coro_create (LoadStep, sizeof (*load));
  if (bg_h == PM_METAL_ASYNC_HANDLE_INVALID
      || load_h == PM_METAL_ASYNC_HANDLE_INVALID)
  {
    pm_metal_log ("metal-async: alloc failed");
    return EFI_OUT_OF_RESOURCES;
  }

  load = (load_state_t *)(UINTN)pm_metal_async_coro_state (load_h);
  if (load == NULL) {
    pm_metal_log ("metal-async: state failed");
    return EFI_OUT_OF_RESOURCES;
  }

  load->expect = (INT32)((METAL_LOAD_WORKERS * (METAL_LOAD_WORKERS + 1u)) / 2u);
  load->step   = LOAD_YIELD;
  load->bg     = pm_metal_async_create_task (bg_h);
  load_task    = pm_metal_async_create_task (load_h);
  if (load->bg == PM_METAL_ASYNC_HANDLE_INVALID
      || load_task == PM_METAL_ASYNC_HANDLE_INVALID)
  {
    pm_metal_log ("metal-async: create_task failed");
    return EFI_OUT_OF_RESOURCES;
  }

  pm_metal_async_task_set_stop_on_done (load_task, 1);

  pm_metal_logf (
    "metal-async: load %u workers (%ux%u) + bg (RR)",
    METAL_LOAD_WORKERS,
    METAL_LOAD_WAVES,
    METAL_LOAD_WAVE_WIDTH
    );

  /* Migrators: task_new + one spawn (never create_task then spawn — double post). */
  for (i = 0; i < n; i++) {
    pm_metal_async_handle_t  add_h;
    add_state_t             *add;
    pm_metal_coro_t         *c;
    pm_metal_task_t         *t;

    add_h = pm_metal_async_coro_create (AddStep, sizeof (*add));
    if (add_h == PM_METAL_ASYNC_HANDLE_INVALID) {
      return EFI_OUT_OF_RESOURCES;
    }

    add = (add_state_t *)(UINTN)pm_metal_async_coro_state (add_h);
    if (add == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    add->in = i;
    c       = pm_metal_async_host_coro (add_h);
    if (c == NULL) {
      return EFI_OUT_OF_RESOURCES;
    }

    t = pm_metal_task_new (c);
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

  if (pm_metal_async_task_status (load_task) != PM_METAL_DONE) {
    pm_metal_logf (
      "metal-async: load not DONE (status=%d total=%d expect=%d)",
      pm_metal_async_task_status (load_task),
      load->total,
      load->expect
      );
    return EFI_DEVICE_ERROR;
  }

  if (load->yield_ok != 1) {
    pm_metal_log ("metal-async: yield fairness failed");
    return EFI_DEVICE_ERROR;
  }

  pm_metal_log ("metal-async: yield ok");
  pm_metal_logf (
    "metal-async: load ok (sum=%d workers=%u)",
    load->total,
    METAL_LOAD_WORKERS
    );
  pm_metal_log ("metal-async: ok");

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
