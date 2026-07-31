/** @file
  Per-CPU cooperative runner — lock-free ready-ring pump + runloop
  (docs/COOP_MEMORY.md). Each CPU owns a ring of tagged pointers; a claim
  is a single CAS, so any idle CPU may claim from any ring (work-stealing
  falls out of the claim primitive itself — there is no separate lock to
  acquire to look at a neighbor's ring).
**/
#ifndef PM_METAL_RUNTIME_RUN_RUN_H_
#define PM_METAL_RUNTIME_RUN_RUN_H_

#include <pymergetic/metal/util/fourcc.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_MEM_ID_INBOX(k) (PM_METAL_UTIL_FOURCC('i', 'n', 'b', 'x') + (uint32_t)(k))

#define PM_METAL_RUN_MSG_NOP  0u
#define PM_METAL_RUN_MSG_PING 1u
#define PM_METAL_RUN_MSG_ADD  2u
#define PM_METAL_RUN_MSG_STOP 3u
#define PM_METAL_RUN_MSG_TASK 4u /* cookie = pm_metal_task_t * (step) */

/* impl: efi|bios */
int pm_metal_run_init(unsigned n_cpus);

/* impl: efi|bios */
int pm_metal_run_post(unsigned cpu, uint32_t op, uint32_t arg0);

/** Like post, with a pointer cookie (task spawn). */
/* impl: efi|bios */
int pm_metal_run_post_ex(unsigned cpu, uint32_t op, uint32_t arg0, uint64_t cookie);

/* impl: efi|bios */
void pm_metal_run_loop(unsigned cpu);
/* impl: efi|bios */
void pm_metal_run_enter(unsigned cpu);

/**
 * Drop queued inbox messages (e.g. leftover MSG_STOP after smoke).
 * Call before the owned-phase run_enter / seed.
 */
/* impl: efi|bios */
void pm_metal_run_clear_inboxes(void);

/**
 * Non-blocking: drain pending inbox messages on `cpu`, then poll timers.
 * For shell/wasm pumps when loopers are not sitting in run_loop.
 */
/* impl: efi|bios */
void pm_metal_run_poll(unsigned cpu);

/**
 * Drain every CPU inbox + poll timers once (FCFS — no CPU0-only Extrawurst).
 * For shell/wasm session pumps on the BSP while equal runners share work.
 */
/* impl: efi|bios */
void pm_metal_run_poll_all(void);

/* impl: efi|bios */
int pm_metal_run_check(unsigned n_cpus, uint32_t expect_add);

/* impl: efi|bios */
uint32_t pm_metal_run_done(unsigned cpu);
/* impl: efi|bios */
uint32_t pm_metal_run_sum(unsigned cpu);

/**
 * Rolling busy % (0-100) for `cpu`'s runner over the last ~500ms window —
 * time spent inside pm_metal_task_step vs. idle CpuPause/poll_timers.
 * Diagnostics only (see docs/COOP_MEMORY.md).
 */
/* impl: efi|bios */
uint32_t pm_metal_run_busy_pct(unsigned cpu);

/**
 * Diagnostics only: how many times another CPU claimed a slot from
 * `cpu`'s ring while `cpu` itself was busy (or before it got to it).
 * Non-zero means work-stealing is actively rebalancing load.
 */
/* impl: efi|bios */
uint32_t pm_metal_run_steal_count(unsigned cpu);

/**
 * Diagnostics only: how many claim CAS attempts on `cpu`'s ring lost the
 * race to a concurrent claimant. High values mean heavy claim contention
 * (many CPUs fighting over the same few ready slots), not correctness —
 * a lost claim just retries the scan.
 */
/* impl: efi|bios */
uint32_t pm_metal_run_claim_retries(unsigned cpu);

#ifdef __cplusplus
}
#endif

#endif /* PM_METAL_RUNTIME_RUN_RUN_H_ */
