/** @file
  Tasks — asyncio.Task over a root coro. (impl: efi)
**/
#include <task/task.h>
#include <run/run.h>
#include <mem/mem.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/CpuLib.h>
#include <Library/SynchronizationLib.h>

STATIC volatile UINT32  mCreateTaskRr;

STATIC
unsigned
MetalPickCpu (
  VOID
  )
{
  unsigned  n;
  UINT32    ticket;

  n = pm_metal_mem_n_cpus ();
  if (n <= 1) {
    return 0;
  }

  ticket = InterlockedIncrement (&mCreateTaskRr);
  return (unsigned)((ticket - 1u) % n);
}

STATIC
void
MetalWakeTaskWaiter (
  pm_metal_task_t  *task
  )
{
  pm_metal_coro_t  *w;

  if (task == NULL) {
    return;
  }

  w = task->waiter;
  task->waiter = NULL;
  if (w == NULL || w->owner == NULL) {
    return;
  }

  (VOID)pm_metal_task_spawn (w->owner, w->owner->cpu);
}

STATIC
void
MetalFinishTask (
  pm_metal_task_t   *task,
  pm_metal_status_t  st
  )
{
  if (task->coro != NULL) {
    task->result = task->coro->result;
  }

  /* Status before wake — pairs with await_task's waiter-then-recheck. */
  task->status = st;
  MemoryFence ();
  MetalWakeTaskWaiter (task);

  if (task->stop_on_done) {
    unsigned  n;
    unsigned  i;

    /* Drain every looper — workers may have been spawned off cpu0. */
    n = pm_metal_mem_n_cpus ();
    if (n == 0) {
      n = 1;
    }

    for (i = 0; i < n; i++) {
      (VOID)pm_metal_run_post (i, PM_METAL_RUN_MSG_STOP, 0);
    }
  }
}

pm_metal_task_t *
pm_metal_task_new (
  pm_metal_coro_t  *coro
  )
{
  pm_metal_task_t  *t;

  if (coro == NULL) {
    return NULL;
  }

  t = (pm_metal_task_t *)pm_metal_mem_alloc (
                           sizeof (*t),
                           PM_METAL_MEM_HEAP,
                           PM_METAL_MEM_ID_NONE
                           );
  if (t == NULL) {
    return NULL;
  }

  ZeroMem (t, sizeof (*t));
  t->coro     = coro;
  t->status   = PM_METAL_PENDING;
  coro->owner = t;
  return t;
}

pm_metal_task_t *
pm_metal_create_task (
  pm_metal_coro_t  *coro
  )
{
  pm_metal_task_t  *t;
  unsigned          cpu;

  t = pm_metal_task_new (coro);
  if (t == NULL) {
    return NULL;
  }

  cpu = MetalPickCpu ();
  if (pm_metal_task_spawn (t, cpu) != 0) {
    t->coro->owner = NULL;
    pm_metal_mem_free (t);
    return NULL;
  }

  return t;
}

void
pm_metal_task_destroy (
  pm_metal_task_t  *task
  )
{
  if (task == NULL) {
    return;
  }

  /*
    Finisher may still be inside task_step after MetalFinishTask woke us.
    Wait until busy clears before freeing.
  */
  while (task->busy != 0) {
    CpuPause ();
  }

  if (task->coro != NULL) {
    pm_metal_coro_close (task->coro);
    task->coro = NULL;
  }

  pm_metal_mem_free (task);
}

int
pm_metal_task_spawn (
  pm_metal_task_t  *task,
  unsigned          cpu
  )
{
  if (task == NULL) {
    return -1;
  }

  task->cpu = cpu;
  if (task->status != PM_METAL_DONE
      && task->status != PM_METAL_ERROR
      && task->status != PM_METAL_CANCELLED)
  {
    task->status = PM_METAL_PENDING;
  }

  return pm_metal_run_post_ex (
           cpu,
           PM_METAL_RUN_MSG_TASK,
           0,
           (uint64_t)(UINTN)(VOID *)task
           );
}

STATIC
pm_metal_status_t
MetalTaskStepLocked (
  pm_metal_task_t  *task
  )
{
  if (task->status == PM_METAL_DONE
      || task->status == PM_METAL_ERROR
      || task->status == PM_METAL_CANCELLED)
  {
    return task->status;
  }

  if (task->cancelled) {
    MetalFinishTask (task, PM_METAL_CANCELLED);
    return PM_METAL_CANCELLED;
  }

  for (;;) {
    pm_metal_coro_t   *leaf;
    pm_metal_status_t  st;

    leaf = task->coro;
    while (leaf->awaiting != NULL) {
      leaf = leaf->awaiting;
    }

    if (leaf->status == PM_METAL_DONE
        || leaf->status == PM_METAL_ERROR
        || leaf->status == PM_METAL_CANCELLED)
    {
      pm_metal_coro_t  *parent;
      pm_metal_coro_t  *child;

      parent = leaf->waiter;
      if (parent == NULL) {
        MetalFinishTask (task, leaf->status);
        return task->status;
      }

      /*
        Hand result to parent, resume parent, then close the nested child.
        Parent must copy out of parent->result in that resume if it needs
        data that lived inside the child.
      */
      child            = leaf;
      parent->result   = child->result;
      parent->awaiting = NULL;
      child->waiter    = NULL;

      st = parent->fn (parent);
      parent->status = st;
      pm_metal_coro_close (child);

      if (st == PM_METAL_WAITING) {
        if (parent->awaiting != NULL) {
          continue;
        }

        task->status = PM_METAL_WAITING;
        return PM_METAL_WAITING;
      }

      if (st == PM_METAL_PENDING) {
        task->status = PM_METAL_PENDING;
        (VOID)pm_metal_task_spawn (task, task->cpu);
        return PM_METAL_PENDING;
      }

      if (st == PM_METAL_DONE
          || st == PM_METAL_ERROR
          || st == PM_METAL_CANCELLED)
      {
        continue;
      }

      MetalFinishTask (task, PM_METAL_ERROR);
      return PM_METAL_ERROR;
    }

    st = leaf->fn (leaf);
    leaf->status = st;

    if (st == PM_METAL_WAITING) {
      if (leaf->awaiting != NULL) {
        continue;
      }

      task->status = PM_METAL_WAITING;
      return PM_METAL_WAITING;
    }

    if (st == PM_METAL_PENDING) {
      task->status = PM_METAL_PENDING;
      (VOID)pm_metal_task_spawn (task, task->cpu);
      return PM_METAL_PENDING;
    }

    if (st == PM_METAL_DONE
        || st == PM_METAL_ERROR
        || st == PM_METAL_CANCELLED)
    {
      continue;
    }

    MetalFinishTask (task, PM_METAL_ERROR);
    return PM_METAL_ERROR;
  }
}

pm_metal_status_t
pm_metal_task_step (
  pm_metal_task_t  *task
  )
{
  pm_metal_status_t  st;

  if (task == NULL || task->coro == NULL) {
    return PM_METAL_ERROR;
  }

  /*
    Duplicate inbox posts (migrate, timer+wake) must not step in parallel —
    that races coro/heap state across CPUs.

    EDK2 InterlockedCompareExchange32 (Value, Compare, Exchange).
  */
  if (InterlockedCompareExchange32 (&task->busy, 0, 1) != 0) {
    (VOID)InterlockedCompareExchange32 (&task->pending_wake, 0, 1);
    return task->status;
  }

  do {
    (VOID)InterlockedCompareExchange32 (&task->pending_wake, 1, 0);
    st = MetalTaskStepLocked (task);
    if (st == PM_METAL_DONE
        || st == PM_METAL_ERROR
        || st == PM_METAL_CANCELLED)
    {
      break;
    }
  } while (task->pending_wake != 0);

  (VOID)InterlockedCompareExchange32 (&task->busy, 1, 0);
  if (InterlockedCompareExchange32 (&task->pending_wake, 1, 0) != 0) {
    if (st != PM_METAL_DONE
        && st != PM_METAL_ERROR
        && st != PM_METAL_CANCELLED)
    {
      (VOID)pm_metal_task_spawn (task, task->cpu);
    }
  }

  return st;
}

pm_metal_status_t
pm_metal_await_task (
  pm_metal_coro_t  *self,
  pm_metal_task_t  *task
  )
{
  if (self == NULL || task == NULL) {
    return PM_METAL_ERROR;
  }

  /*
    Install waiter before re-checking status so a concurrent finish cannot
    complete between the check and the publish (lost wakeup → hang).
  */
  task->waiter = self;
  MemoryFence ();

  if (task->status == PM_METAL_DONE
      || task->status == PM_METAL_ERROR
      || task->status == PM_METAL_CANCELLED)
  {
    task->waiter = NULL;
    self->result = task->result;
    return task->status;
  }

  self->status = PM_METAL_WAITING;
  return PM_METAL_WAITING;
}

pm_metal_status_t
pm_metal_task_status (
  pm_metal_task_t  *task
  )
{
  return (task != NULL) ? task->status : PM_METAL_ERROR;
}

void *
pm_metal_task_result (
  pm_metal_task_t  *task
  )
{
  return (task != NULL) ? task->result : NULL;
}

void
pm_metal_task_cancel (
  pm_metal_task_t  *task
  )
{
  if (task == NULL) {
    return;
  }

  task->cancelled = 1;
  if (task->status != PM_METAL_DONE
      && task->status != PM_METAL_ERROR
      && task->status != PM_METAL_CANCELLED)
  {
    (VOID)pm_metal_task_spawn (task, task->cpu);
  }
}

int
pm_metal_task_run (
  pm_metal_coro_t  *main,
  unsigned          cpu
  )
{
  pm_metal_task_t  *t;

  if (main == NULL) {
    return -1;
  }

  t = pm_metal_create_task (main);
  if (t == NULL) {
    return -1;
  }

  t->stop_on_done = 1;
  if (t->cpu != cpu) {
    (VOID)pm_metal_task_spawn (t, cpu);
  }

  pm_metal_run_enter (cpu);

  return (t->status == PM_METAL_DONE) ? 0 : -1;
}
