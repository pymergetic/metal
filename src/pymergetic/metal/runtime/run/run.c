/** @file
  Per-CPU lock-free ready ring + cooperative drain (heap scratch, task resume).
  (impl: efi|bios)

  Each CPU owns a ring of tagged 64-bit slots (see PM_METAL_RING_* below).
  Push/claim are single CAS instructions -- there is no SPIN_LOCK anywhere
  in this file. A CPU normally only touches its own ring (cache-local, same
  as a per-CPU inbox would be), but because a slot claim is just an atomic
  compare-exchange rather than a lock acquisition, *any* CPU may claim a
  READY slot from *any* ring -- an idle CPU "steals" a neighbor's queued
  work with no extra synchronization primitive beyond the claim itself.
  See docs/COOP_MEMORY.md.
**/
#include <runtime/run/run.h>
#include <runtime/stack/stack.h>
#include <runtime/task/task.h>
#include <runtime/coro/coro.h>
#include <pymergetic/metal/runtime/mem/mem.h>
#include <runtime/time/time.h>
#include <runtime/time/cpu.h>
#include <runtime/slot/slot_table.h>

#include <assert.h>
#include <stdbool.h>
#include <string.h>

#define PM_METAL_RUN_INBOX_DEPTH 64

/*
 * Tagged 64-bit slot: low 48 bits = payload (task pointer, or a small
 * immediate for ADD/PING/STOP), next 2 bits = state, next 3 bits = kind.
 * EMPTY is exactly the all-zero word, which keeps the "is this slot free"
 * check and the CAS expected-value both a bare 0.
 *
 * The 48-bit payload assumes canonical low addresses (top 16 bits zero) --
 * true for every pointer here, since they all come from the UEFI
 * boot-services heap (pm_metal_mem_alloc), which lives far below bit 47.
 * MetalRingAssertPayload ASSERTs this instead of silently truncating.
 */
#define PM_METAL_RING_PAYLOAD_MASK 0x0000FFFFFFFFFFFFull
#define PM_METAL_RING_STATE_SHIFT  48u
#define PM_METAL_RING_STATE_MASK   0x3ull
#define PM_METAL_RING_KIND_SHIFT   50u
#define PM_METAL_RING_KIND_MASK    0x7ull

#define PM_METAL_RING_STATE_EMPTY   0ull
#define PM_METAL_RING_STATE_READY   1ull
#define PM_METAL_RING_STATE_CLAIMED 2ull

#define PM_METAL_RING_KIND_TASK 0ull /* payload = pm_metal_task_t*  (step)  */
#define PM_METAL_RING_KIND_ADD  1ull /* payload = arg0              (test)  */
#define PM_METAL_RING_KIND_PING 2ull /* payload = 0                        */
#define PM_METAL_RING_KIND_STOP 3ull /* payload = 0 — not stealable, below  */
#define PM_METAL_RING_KIND_NOP  4ull /* payload = 0                        */

typedef struct {
  /* aligned(8): i386's default uint64_t alignment is 4, but the CAS this
   * word goes through needs real 8-byte alignment for cmpxchg8b atomicity. */
  volatile uint64_t word __attribute__((aligned(8)));
  uint8_t           _pad[56]; /* pad the slot to a full cache line */
} pm_metal_ring_slot_t;

typedef struct {
  /* Soft hint only — where to start the next scan. Racy reads/writes are
     fine: correctness lives entirely in the per-slot CAS, this just avoids
     rescanning from slot 0 every time. */
  volatile uint32_t hint;
  volatile uint32_t done_count;
  volatile uint32_t sum;
  volatile uint32_t steal_count;   /* diagnostics: claims by a non-owner */
  volatile uint32_t claim_retries; /* diagnostics: lost claim CAS races  */
  /* Rolling busy % (diagnostics only — see pm_metal_run_busy_pct).
   * aligned(8): see pm_metal_ring_slot_t.word above — same i386 CAS need. */
  volatile uint64_t    busy_win_start_us __attribute__((aligned(8)));
  volatile uint64_t    busy_us_acc __attribute__((aligned(8)));
  uint32_t             busy_pct;
  pm_metal_ring_slot_t slots[PM_METAL_RUN_INBOX_DEPTH];
} pm_metal_run_inbox_t;

/* Rolling window for busy_pct refresh — long enough to smooth spikes. */
#define PM_METAL_RUN_BUSY_WIN_US 500000ull

static pm_metal_run_inbox_t *MetalInbox(unsigned cpu)
{
  return (pm_metal_run_inbox_t *)pm_metal_mem_lookup(PM_METAL_MEM_ID_INBOX(cpu));
}

static uint64_t MetalRingPack(uint64_t kind, uint64_t state, uint64_t payload)
{
  return (payload & PM_METAL_RING_PAYLOAD_MASK) |
         ((state & PM_METAL_RING_STATE_MASK) << PM_METAL_RING_STATE_SHIFT) |
         ((kind & PM_METAL_RING_KIND_MASK) << PM_METAL_RING_KIND_SHIFT);
}

#define MetalRingState(w)   (((w) >> PM_METAL_RING_STATE_SHIFT) & PM_METAL_RING_STATE_MASK)
#define MetalRingKind(w)    (((w) >> PM_METAL_RING_KIND_SHIFT) & PM_METAL_RING_KIND_MASK)
#define MetalRingPayload(w) ((w) & PM_METAL_RING_PAYLOAD_MASK)

static void MetalRingAssertPayload(uint64_t payload)
{
  assert((payload & ~PM_METAL_RING_PAYLOAD_MASK) == 0);
}

/**
 * Atomic add of an arbitrary delta to a uint32_t — EDK2 only gives us
 * Increment/Decrement (delta==1) and CompareExchange, so arbitrary deltas
 * (the ADD self-test, busy-time accumulation) go through a CAS loop.
 */
static void MetalAtomicAdd32(volatile uint32_t *v, uint32_t delta)
{
  uint32_t old;

  if (delta == 0) {
    return;
  }

  do {
    old = *v;
  } while (pm_metal_slot_port_cas32(v, old, old + delta) != old);
}

static void MetalAtomicAdd64(volatile uint64_t *v, uint64_t delta)
{
  uint64_t old;

  if (delta == 0) {
    return;
  }

  do {
    old = *v;
  } while (pm_metal_slot_port_cas64(v, old, old + delta) != old);
}

/**
 * Roll the busy window forward when it has matured, publishing busy_pct.
 * Multiple CPUs may now touch the same ring's busy accounting (a stealer
 * runs pm_metal_task_step for a ring it doesn't own) — the window-roll
 * transition and the accumulator drain are each done via their own CAS so
 * two concurrent touches can't double-roll or lose an add. Diagnostics
 * only; a lost race here just delays a busy_pct refresh by one window.
 */
static void MetalRunBusyTouch(pm_metal_run_inbox_t *in, uint64_t now)
{
  uint64_t start;
  uint64_t elapsed;
  uint64_t acc;
  uint64_t pct;

  if (in == NULL) {
    return;
  }

  start = in->busy_win_start_us;
  if (start == 0) {
    (void)pm_metal_slot_port_cas64(&in->busy_win_start_us, 0, now);
    return;
  }

  elapsed = now - start;
  if (elapsed < PM_METAL_RUN_BUSY_WIN_US) {
    return;
  }

  if (pm_metal_slot_port_cas64(&in->busy_win_start_us, start, now) != start) {
    return; /* another CPU already rolled this window */
  }

  do {
    acc = in->busy_us_acc;
  } while (pm_metal_slot_port_cas64(&in->busy_us_acc, acc, 0) != acc);

  pct          = (acc * 100ull) / elapsed;
  in->busy_pct = (pct > 100ull) ? 100u : (uint32_t)pct;
}

/** Try to push (kind, payload) into the first EMPTY slot of `in`. */
static intptr_t MetalRingTryPush(pm_metal_run_inbox_t *in, uint64_t kind, uint64_t payload)
{
  uint32_t start;
  uint32_t i;
  uint32_t idx;
  uint64_t ready;

  if (in == NULL) {
    return -1;
  }

  MetalRingAssertPayload(payload);
  ready = MetalRingPack(kind, PM_METAL_RING_STATE_READY, payload);
  start = in->hint;

  for (i = 0; i < PM_METAL_RUN_INBOX_DEPTH; i++) {
    idx = (start + i) % PM_METAL_RUN_INBOX_DEPTH;
    if (pm_metal_slot_port_cas64(&in->slots[idx].word, 0, ready) == 0) {
      in->hint = (idx + 1u) % PM_METAL_RUN_INBOX_DEPTH;
      return 0;
    }
  }

  return -1; /* ring full */
}

/**
 * Push into `home`; if full (rare — depth 64), spill to a neighbor CPU's
 * ring rather than dropping the message. Symmetric with the consumer-side
 * steal below: any CPU may claim from any ring, so it is equally fine for
 * a push to land somewhere other than its requested home.
 */
static intptr_t MetalRingSpillPush(unsigned home_cpu, uint64_t kind, uint64_t payload)
{
  unsigned n;
  unsigned i;

  if (MetalRingTryPush(MetalInbox(home_cpu), kind, payload) == 0) {
    return 0;
  }

  n = pm_metal_mem_n_cpus();
  if (n == 0) {
    n = 1;
  }

  for (i = 1; i < n; i++) {
    if (MetalRingTryPush(MetalInbox((home_cpu + i) % n), kind, payload) == 0) {
      return 0;
    }
  }

  return -1;
}

/**
 * Try to claim one READY slot of `in`, scanning forward from `start`.
 * `allow_stop` gates whether a STOP-tagged slot is eligible: STOP carries
 * per-runner loop-termination semantics (pm_metal_run_loop returns when
 * *its own* ring yields one), so a stealer scanning a foreign ring must
 * skip over it rather than consuming it — otherwise the ring's actual
 * owner would never see its own stop and would spin forever.
 */
static intptr_t MetalRingTryClaim(
  pm_metal_run_inbox_t *in, uint32_t start, bool allow_stop, uint32_t *idx_out, uint64_t *word_out)
{
  uint32_t i;
  uint32_t idx;
  uint64_t cur;
  uint64_t claimed;

  if (in == NULL || idx_out == NULL || word_out == NULL) {
    return -1;
  }

  for (i = 0; i < PM_METAL_RUN_INBOX_DEPTH; i++) {
    idx = (start + i) % PM_METAL_RUN_INBOX_DEPTH;
    cur = in->slots[idx].word;
    if (MetalRingState(cur) != PM_METAL_RING_STATE_READY) {
      continue;
    }

    if (!allow_stop && MetalRingKind(cur) == PM_METAL_RING_KIND_STOP) {
      continue;
    }

    claimed = MetalRingPack(MetalRingKind(cur), PM_METAL_RING_STATE_CLAIMED, MetalRingPayload(cur));
    if (pm_metal_slot_port_cas64(&in->slots[idx].word, cur, claimed) == cur) {
      in->hint  = (idx + 1u) % PM_METAL_RUN_INBOX_DEPTH;
      *idx_out  = idx;
      *word_out = claimed;
      return 0;
    }

    /* Lost the race — another claimant (or a push we misread) got there
       first. Diagnostics only; keep scanning for the next candidate. */
    pm_metal_atomic_inc32(&in->claim_retries);
  }

  return -1; /* nothing eligible right now */
}

/**
 * Claim + process exactly one slot of `in`. Returns -1 (nothing to do),
 * 0 (processed a non-STOP slot), or 1 (processed a STOP slot).
 */
static intptr_t MetalRingStep(pm_metal_run_inbox_t *in, uint32_t start, bool allow_stop)
{
  uint32_t idx;
  uint64_t word;
  uint64_t kind;
  uint64_t payload;
  intptr_t rc;

  if (MetalRingTryClaim(in, start, allow_stop, &idx, &word) != 0) {
    return -1;
  }

  kind    = MetalRingKind(word);
  payload = MetalRingPayload(word);
  rc      = 0;

  switch (kind) {
  case PM_METAL_RING_KIND_TASK:
    if (payload != 0) {
      pm_metal_task_t *task;
      uint64_t         t0;
      uint64_t         t1;

      task = (pm_metal_task_t *)(void *)(uintptr_t)payload;
      t0   = pm_metal_time_mono_us();
      (void)pm_metal_task_step(task);
      t1 = pm_metal_time_mono_us();
      MetalAtomicAdd64(&in->busy_us_acc, t1 - t0);
      pm_metal_task_unref(task);
    }

    pm_metal_atomic_inc32(&in->done_count);
    break;

  case PM_METAL_RING_KIND_ADD:
    MetalAtomicAdd32(&in->sum, (uint32_t)payload);
    pm_metal_atomic_inc32(&in->done_count);
    break;

  case PM_METAL_RING_KIND_STOP:
    pm_metal_atomic_inc32(&in->done_count);
    rc = 1;
    break;

  case PM_METAL_RING_KIND_PING:
  case PM_METAL_RING_KIND_NOP:
  default:
    pm_metal_atomic_inc32(&in->done_count);
    break;
  }

  in->slots[idx].word = 0; /* release — we exclusively own a CLAIMED slot */
  return rc;
}

int pm_metal_run_init(unsigned n_cpus)
{
  unsigned i;

  if (n_cpus == 0) {
    return -1;
  }

  if (sizeof(pm_metal_run_inbox_t) > 8192) {
    return -1;
  }

  if (pm_metal_stack_init(n_cpus) != 0) {
    return -1;
  }

  /* TSC calibrate on BSP before any AP touches time/sleep. */
  pm_metal_time_init();
  /* Timer list lock before any worker can arm sleep/wait_for. */
  pm_metal_coro_timers_init();

  for (i = 0; i < n_cpus; i++) {
    pm_metal_run_inbox_t *in;

    /* Inbox in heap (published); not a separate SHARED slab. */
    in = (pm_metal_run_inbox_t *)pm_metal_mem_alloc(
      sizeof(pm_metal_run_inbox_t), PM_METAL_MEM_HEAP, PM_METAL_MEM_ID_INBOX(i));
    if (in == NULL) {
      return -1;
    }

    memset(in, 0, sizeof(*in));
  }

  return 0;
}

int pm_metal_run_post(unsigned cpu, uint32_t op, uint32_t arg0)
{
  uint64_t kind;
  uint64_t payload;

  switch (op) {
  case PM_METAL_RUN_MSG_ADD:
    kind    = PM_METAL_RING_KIND_ADD;
    payload = arg0;
    break;

  case PM_METAL_RUN_MSG_PING:
    kind    = PM_METAL_RING_KIND_PING;
    payload = 0;
    break;

  case PM_METAL_RUN_MSG_STOP:
    kind    = PM_METAL_RING_KIND_STOP;
    payload = 0;
    break;

  case PM_METAL_RUN_MSG_TASK:
    kind    = PM_METAL_RING_KIND_TASK;
    payload = 0; /* no cookie via plain post — use post_ex */
    break;

  default:
    kind    = PM_METAL_RING_KIND_NOP;
    payload = 0;
    break;
  }

  return (int)MetalRingSpillPush(cpu, kind, payload);
}

int pm_metal_run_post_ex(unsigned cpu, uint32_t op, uint32_t arg0, uint64_t cookie)
{
  uint64_t kind;

  (void)arg0;
  kind = (op == PM_METAL_RUN_MSG_TASK) ? PM_METAL_RING_KIND_TASK : PM_METAL_RING_KIND_NOP;
  return (int)MetalRingSpillPush(cpu, kind, (uint64_t)cookie);
}

void pm_metal_run_loop(unsigned cpu)
{
  pm_metal_run_inbox_t *in;
  unsigned              n;
  intptr_t              rc;

  pm_metal_mem_set_cpu(cpu);
  in = MetalInbox(cpu);
  if (in == NULL) {
    return;
  }

  n = pm_metal_mem_n_cpus();
  if (n == 0) {
    n = 1;
  }

  for (;;) {
    rc = MetalRingStep(in, in->hint, true);
    if (rc == 1) {
      return; /* our own STOP — this runner is done */
    }

    if (rc == 0) {
      MetalRunBusyTouch(in, pm_metal_time_mono_us());
      continue;
    }

    /*
     * Own ring empty — steal one ready item from a neighbor before idling.
     * No lock: the claim CAS is the only synchronization needed, whether
     * the claimant is the ring's home CPU or a stealer. STOP is never
     * stolen (see MetalRingTryClaim) so this can't skip another runner's
     * termination signal.
     */
    {
      unsigned i;
      int      stole;

      stole = 0;
      for (i = 1; i < n; i++) {
        unsigned              victim_cpu;
        pm_metal_run_inbox_t *victim;

        victim_cpu = (cpu + i) % n;
        victim     = MetalInbox(victim_cpu);
        if (victim == NULL || victim == in) {
          continue;
        }

        rc = MetalRingStep(victim, victim->hint + (PM_METAL_RUN_INBOX_DEPTH / 2u), false);
        if (rc >= 0) {
          pm_metal_atomic_inc32(&victim->steal_count);
          MetalRunBusyTouch(victim, pm_metal_time_mono_us());
          stole = 1;
          break;
        }
      }

      if (stole) {
        continue;
      }
    }

    pm_metal_coro_poll_timers();
    MetalRunBusyTouch(in, pm_metal_time_mono_us());
    pm_metal_cpu_pause();
  }
}

void pm_metal_run_enter(unsigned cpu)
{
  pm_metal_stack_call(cpu, pm_metal_run_loop);
}

void pm_metal_run_clear_inboxes(void)
{
  unsigned n;
  unsigned i;

  n = pm_metal_mem_n_cpus();
  if (n == 0) {
    n = 1;
  }

  for (i = 0; i < n; i++) {
    pm_metal_run_inbox_t *in;
    uint32_t              idx;
    uint64_t              word;

    in = MetalInbox(i);
    if (in == NULL) {
      continue;
    }

    while (MetalRingTryClaim(in, in->hint, true, &idx, &word) == 0) {
      if (MetalRingKind(word) == PM_METAL_RING_KIND_TASK) {
        uint64_t payload;

        payload = MetalRingPayload(word);
        if (payload != 0) {
          pm_metal_task_unref((pm_metal_task_t *)(void *)(uintptr_t)payload);
        }
      }

      in->slots[idx].word = 0;
    }
  }
}

/** Bound drain — avoid starving the shell if a ring is hot. */
static void MetalRingDrainBounded(pm_metal_run_inbox_t *in)
{
  uint32_t n;

  if (in == NULL) {
    return;
  }

  for (n = 0; n < PM_METAL_RUN_INBOX_DEPTH; n++) {
    if (MetalRingStep(in, in->hint, true) < 0) {
      break;
    }
  }

  MetalRunBusyTouch(in, pm_metal_time_mono_us());
}

void pm_metal_run_poll(unsigned cpu)
{
  pm_metal_run_inbox_t *in;

  in = MetalInbox(cpu);
  MetalRingDrainBounded(in);
  /* Timers may post TASK wakes — drain again so one pump can resume. */
  pm_metal_coro_poll_timers();
  MetalRingDrainBounded(in);
}

void pm_metal_run_poll_all(void)
{
  unsigned n;
  unsigned i;

  n = pm_metal_mem_n_cpus();
  if (n == 0) {
    n = 1;
  }

  for (i = 0; i < n; i++) {
    MetalRingDrainBounded(MetalInbox(i));
  }

  pm_metal_coro_poll_timers();

  for (i = 0; i < n; i++) {
    MetalRingDrainBounded(MetalInbox(i));
  }
}

int pm_metal_run_check(unsigned n_cpus, uint32_t expect_add)
{
  unsigned i;

  for (i = 0; i < n_cpus; i++) {
    pm_metal_run_inbox_t *in;

    in = MetalInbox(i);
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

uint32_t pm_metal_run_done(unsigned cpu)
{
  pm_metal_run_inbox_t *in;

  in = MetalInbox(cpu);
  return (in != NULL) ? in->done_count : 0;
}

uint32_t pm_metal_run_sum(unsigned cpu)
{
  pm_metal_run_inbox_t *in;

  in = MetalInbox(cpu);
  return (in != NULL) ? in->sum : 0;
}

uint32_t pm_metal_run_busy_pct(unsigned cpu)
{
  pm_metal_run_inbox_t *in;

  in = MetalInbox(cpu);
  return (in != NULL) ? in->busy_pct : 0;
}

uint32_t pm_metal_run_steal_count(unsigned cpu)
{
  pm_metal_run_inbox_t *in;

  in = MetalInbox(cpu);
  return (in != NULL) ? in->steal_count : 0;
}

uint32_t pm_metal_run_claim_retries(unsigned cpu)
{
  pm_metal_run_inbox_t *in;

  in = MetalInbox(cpu);
  return (in != NULL) ? in->claim_retries : 0;
}
