/** @file
  Per-CPU runner: inbox + cooperative drain (heap scratch, coro resume).
**/
#include "metal_run.h"
#include "metal_stack.h"
#include "metal_coro.h"

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CpuLib.h>

#include "mem/metal_mem.h"

#define METAL_RUN_INBOX_DEPTH  16

typedef struct {
  UINT32    op;
  UINT32    arg0;
  UINTN     cookie;
  UINT32    seq;
} metal_msg_t;

typedef struct {
  volatile UINT32  head;
  volatile UINT32  tail;
  UINT32           done_count;
  UINT32           sum;
  UINT32           seq_in;
  metal_msg_t      slots[METAL_RUN_INBOX_DEPTH];
} metal_inbox_t;

STATIC
metal_inbox_t *
MetalInbox (
  unsigned  cpu
  )
{
  return (metal_inbox_t *)metal_lookup (METAL_ID_INBOX (cpu));
}

STATIC
INTN
MetalInboxPush (
  metal_inbox_t  *in,
  UINT32          op,
  UINT32          arg0,
  UINTN           cookie
  )
{
  UINT32  tail;
  UINT32  next;
  UINT32  head;

  if (in == NULL) {
    return -1;
  }

  tail = in->tail;
  next = (tail + 1) % METAL_RUN_INBOX_DEPTH;
  head = in->head;
  if (next == head) {
    return -1;
  }

  in->slots[tail].op     = op;
  in->slots[tail].arg0   = arg0;
  in->slots[tail].cookie = cookie;
  in->slots[tail].seq    = ++in->seq_in;
  MemoryFence ();
  in->tail = next;
  MemoryFence ();
  return 0;
}

STATIC
INTN
MetalInboxPop (
  metal_inbox_t  *in,
  metal_msg_t    *out
  )
{
  UINT32  head;
  UINT32  tail;

  if (in == NULL || out == NULL) {
    return -1;
  }

  head = in->head;
  tail = in->tail;
  if (head == tail) {
    return -1;
  }

  *out = in->slots[head];
  MemoryFence ();
  in->head = (head + 1) % METAL_RUN_INBOX_DEPTH;
  MemoryFence ();
  return 0;
}

int
metal_run_init (
  unsigned  n_cpus
  )
{
  unsigned  i;

  if (n_cpus == 0) {
    return -1;
  }

  if (sizeof (metal_inbox_t) > 1024) {
    return -1;
  }

  if (metal_stack_init (n_cpus) != 0) {
    return -1;
  }

  for (i = 0; i < n_cpus; i++) {
    metal_inbox_t  *in;

    /* Inbox in heap (published); not a separate SHARED slab. */
    in = (metal_inbox_t *)metal_alloc (
                            sizeof (metal_inbox_t),
                            METAL_MEM_HEAP,
                            METAL_ID_INBOX (i)
                            );
    if (in == NULL) {
      return -1;
    }

    ZeroMem (in, sizeof (*in));
  }

  return 0;
}

int
metal_run_post (
  unsigned  cpu,
  uint32_t  op,
  uint32_t  arg0
  )
{
  return (int)MetalInboxPush (MetalInbox (cpu), op, arg0, 0);
}

int
metal_run_post_ex (
  unsigned  cpu,
  uint32_t  op,
  uint32_t  arg0,
  uint64_t  cookie
  )
{
  return (int)MetalInboxPush (MetalInbox (cpu), op, arg0, (UINTN)cookie);
}

void
metal_run_loop (
  unsigned  cpu
  )
{
  metal_inbox_t  *in;
  metal_msg_t     msg;

  metal_mem_set_cpu (cpu);
  in = MetalInbox (cpu);
  if (in == NULL) {
    return;
  }

  for (;;) {
    if (MetalInboxPop (in, &msg) != 0) {
      CpuPause ();
      continue;
    }

    switch (msg.op) {
      case METAL_MSG_ADD:
        {
          UINT32  *scratch;

          scratch = (UINT32 *)metal_alloc (64, METAL_MEM_HEAP, METAL_ID_NONE);
          if (scratch != NULL) {
            *scratch = msg.arg0;
            in->sum += *scratch;
            metal_free (scratch);
          } else {
            in->sum += msg.arg0;
          }

          in->done_count++;
        }
        break;

      case METAL_MSG_PING:
        in->done_count++;
        break;

      case METAL_MSG_CORO:
        if (msg.cookie != 0) {
          (VOID)metal_coro_resume ((metal_coro_t *)(VOID *)msg.cookie);
        }

        in->done_count++;
        break;

      case METAL_MSG_STOP:
        in->done_count++;
        return;

      default:
        in->done_count++;
        break;
    }
  }
}

void
metal_run_enter (
  unsigned  cpu
  )
{
  metal_stack_call (cpu, metal_run_loop);
}

int
metal_run_check (
  unsigned  n_cpus,
  uint32_t  expect_add
  )
{
  unsigned  i;

  for (i = 0; i < n_cpus; i++) {
    metal_inbox_t  *in;

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
metal_run_done (
  unsigned  cpu
  )
{
  metal_inbox_t  *in;

  in = MetalInbox (cpu);
  return (in != NULL) ? in->done_count : 0;
}

uint32_t
metal_run_sum (
  unsigned  cpu
  )
{
  metal_inbox_t  *in;

  in = MetalInbox (cpu);
  return (in != NULL) ? in->sum : 0;
}
