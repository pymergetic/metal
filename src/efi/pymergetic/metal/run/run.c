/** @file
  Per-CPU runner: inbox + cooperative drain (heap scratch, task resume). (impl: efi)
**/
#include <run/run.h>
#include <stack/stack.h>
#include <task/task.h>
#include <coro/coro.h>
#include <mem/mem.h>
#include <time/time.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CpuLib.h>
#include <Library/SynchronizationLib.h>

#define PM_METAL_RUN_INBOX_DEPTH  64

typedef struct {
  UINT32    op;
  UINT32    arg0;
  UINTN     cookie;
  UINT32    seq;
} pm_metal_run_msg_t;

typedef struct {
  SPIN_LOCK           lock;
  volatile UINT32     head;
  volatile UINT32     tail;
  UINT32              done_count;
  UINT32              sum;
  UINT32              seq_in;
  pm_metal_run_msg_t  slots[PM_METAL_RUN_INBOX_DEPTH];
} pm_metal_run_inbox_t;

STATIC
pm_metal_run_inbox_t *
MetalInbox (
  unsigned  cpu
  )
{
  return (pm_metal_run_inbox_t *)pm_metal_mem_lookup (PM_METAL_MEM_ID_INBOX (cpu));
}

STATIC
INTN
MetalInboxPush (
  pm_metal_run_inbox_t  *in,
  UINT32                 op,
  UINT32                 arg0,
  UINTN                  cookie
  )
{
  UINT32  tail;
  UINT32  next;
  UINT32  head;
  INTN    rc;

  if (in == NULL) {
    return -1;
  }

  AcquireSpinLock (&in->lock);
  tail = in->tail;
  next = (tail + 1) % PM_METAL_RUN_INBOX_DEPTH;
  head = in->head;
  if (next == head) {
    rc = -1;
  } else {
    in->slots[tail].op     = op;
    in->slots[tail].arg0   = arg0;
    in->slots[tail].cookie = cookie;
    in->slots[tail].seq    = ++in->seq_in;
    in->tail = next;
    rc = 0;
  }

  ReleaseSpinLock (&in->lock);
  return rc;
}

STATIC
INTN
MetalInboxPop (
  pm_metal_run_inbox_t  *in,
  pm_metal_run_msg_t    *out
  )
{
  UINT32  head;
  UINT32  tail;
  INTN    rc;

  if (in == NULL || out == NULL) {
    return -1;
  }

  AcquireSpinLock (&in->lock);
  head = in->head;
  tail = in->tail;
  if (head == tail) {
    rc = -1;
  } else {
    *out = in->slots[head];
    in->head = (head + 1) % PM_METAL_RUN_INBOX_DEPTH;
    rc = 0;
  }

  ReleaseSpinLock (&in->lock);
  return rc;
}

int
pm_metal_run_init (
  unsigned  n_cpus
  )
{
  unsigned  i;

  if (n_cpus == 0) {
    return -1;
  }

  if (sizeof (pm_metal_run_inbox_t) > 4096) {
    return -1;
  }

  if (pm_metal_stack_init (n_cpus) != 0) {
    return -1;
  }

  /* TSC calibrate on BSP before any AP touches time/sleep. */
  pm_metal_time_init ();

  for (i = 0; i < n_cpus; i++) {
    pm_metal_run_inbox_t  *in;

    /* Inbox in heap (published); not a separate SHARED slab. */
    in = (pm_metal_run_inbox_t *)pm_metal_mem_alloc (
                                   sizeof (pm_metal_run_inbox_t),
                                   PM_METAL_MEM_HEAP,
                                   PM_METAL_MEM_ID_INBOX (i)
                                   );
    if (in == NULL) {
      return -1;
    }

    ZeroMem (in, sizeof (*in));
    InitializeSpinLock (&in->lock);
  }

  return 0;
}

int
pm_metal_run_post (
  unsigned  cpu,
  uint32_t  op,
  uint32_t  arg0
  )
{
  return (int)MetalInboxPush (MetalInbox (cpu), op, arg0, 0);
}

int
pm_metal_run_post_ex (
  unsigned  cpu,
  uint32_t  op,
  uint32_t  arg0,
  uint64_t  cookie
  )
{
  return (int)MetalInboxPush (MetalInbox (cpu), op, arg0, (UINTN)cookie);
}

void
pm_metal_run_loop (
  unsigned  cpu
  )
{
  pm_metal_run_inbox_t  *in;
  pm_metal_run_msg_t     msg;

  pm_metal_mem_set_cpu (cpu);
  in = MetalInbox (cpu);
  if (in == NULL) {
    return;
  }

  for (;;) {
    if (MetalInboxPop (in, &msg) != 0) {
      pm_metal_coro_poll_timers ();
      CpuPause ();
      continue;
    }

    switch (msg.op) {
      case PM_METAL_RUN_MSG_ADD:
        {
          UINT32  *scratch;

          scratch = (UINT32 *)pm_metal_mem_alloc (
                                64,
                                PM_METAL_MEM_HEAP,
                                PM_METAL_MEM_ID_NONE
                                );
          if (scratch != NULL) {
            *scratch = msg.arg0;
            in->sum += *scratch;
            pm_metal_mem_free (scratch);
          } else {
            in->sum += msg.arg0;
          }

          in->done_count++;
        }
        break;

      case PM_METAL_RUN_MSG_PING:
        in->done_count++;
        break;

      case PM_METAL_RUN_MSG_TASK:
        if (msg.cookie != 0) {
          (VOID)pm_metal_task_step ((pm_metal_task_t *)(VOID *)msg.cookie);
        }

        in->done_count++;
        break;

      case PM_METAL_RUN_MSG_STOP:
        in->done_count++;
        return;

      default:
        in->done_count++;
        break;
    }
  }
}

void
pm_metal_run_enter (
  unsigned  cpu
  )
{
  pm_metal_stack_call (cpu, pm_metal_run_loop);
}

STATIC
VOID
MetalRunPollDrain (
  pm_metal_run_inbox_t  *in
  )
{
  pm_metal_run_msg_t  msg;
  UINT32              n;

  if (in == NULL) {
    return;
  }

  /* Bound drain — avoid starving the shell if inbox is hot. */
  for (n = 0; n < PM_METAL_RUN_INBOX_DEPTH; n++) {
    if (MetalInboxPop (in, &msg) != 0) {
      break;
    }

    switch (msg.op) {
      case PM_METAL_RUN_MSG_TASK:
        if (msg.cookie != 0) {
          (VOID)pm_metal_task_step ((pm_metal_task_t *)(VOID *)msg.cookie);
        }

        in->done_count++;
        break;

      case PM_METAL_RUN_MSG_STOP:
        in->done_count++;
        break;

      case PM_METAL_RUN_MSG_ADD:
      case PM_METAL_RUN_MSG_PING:
      default:
        in->done_count++;
        break;
    }
  }
}

void
pm_metal_run_poll (
  unsigned  cpu
  )
{
  pm_metal_run_inbox_t  *in;

  in = MetalInbox (cpu);
  MetalRunPollDrain (in);
  /* Timers may post TASK wakes — drain again so one pump can resume. */
  pm_metal_coro_poll_timers ();
  MetalRunPollDrain (in);
}

int
pm_metal_run_check (
  unsigned  n_cpus,
  uint32_t  expect_add
  )
{
  unsigned  i;

  for (i = 0; i < n_cpus; i++) {
    pm_metal_run_inbox_t  *in;

    in = MetalInbox (i);
    if (in == NULL) {
      return -1;
    }

    if (in->sum != expect_add) {
      return -1;
    }

    if (in->done_count < 2) {
      return -1;
    }
  }

  return 0;
}

uint32_t
pm_metal_run_done (
  unsigned  cpu
  )
{
  pm_metal_run_inbox_t  *in;

  in = MetalInbox (cpu);
  return (in != NULL) ? in->done_count : 0;
}

uint32_t
pm_metal_run_sum (
  unsigned  cpu
  )
{
  pm_metal_run_inbox_t  *in;

  in = MetalInbox (cpu);
  return (in != NULL) ? in->sum : 0;
}
