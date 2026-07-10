/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PM_PORT_BENCH_H_
#define PM_PORT_BENCH_H_

#include <stdint.h>

void pm_port_bench_init(void);
uint64_t pm_port_bench_now_ns(void);

#endif /* PM_PORT_BENCH_H_ */
