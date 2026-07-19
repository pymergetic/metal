/** @file
  Per-CPU cooperative runner — inbox pump + runloop (docs/COOP_MEMORY.md).
**/
#ifndef METAL_RUN_H_
#define METAL_RUN_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define METAL_ID_INBOX(k)  (0x4d544200u + (uint32_t)(k))

#define METAL_MSG_NOP   0u
#define METAL_MSG_PING  1u
#define METAL_MSG_ADD   2u
#define METAL_MSG_STOP  3u
#define METAL_MSG_CORO  4u   /* cookie = metal_coro_t * */

int metal_run_init (unsigned n_cpus);

int metal_run_post (
  unsigned  cpu,
  uint32_t  op,
  uint32_t  arg0
  );

/** Like post, with a pointer cookie (coro spawn). */
int metal_run_post_ex (
  unsigned   cpu,
  uint32_t   op,
  uint32_t   arg0,
  uint64_t   cookie
  );

void metal_run_loop (unsigned cpu);
void metal_run_enter (unsigned cpu);

int metal_run_check (
  unsigned  n_cpus,
  uint32_t  expect_add
  );

uint32_t metal_run_done (unsigned cpu);
uint32_t metal_run_sum (unsigned cpu);

#ifdef __cplusplus
}
#endif

#endif /* METAL_RUN_H_ */
