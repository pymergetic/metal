/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Fake metal (native_sim) vs real metal (QEMU / HW) — different malloc models.
 */

#ifndef PM_METAL_PORT_PLATFORM_H_
#define PM_METAL_PORT_PLATFORM_H_

#if defined(CONFIG_BOARD_NATIVE_SIM)
#define PM_METAL_PORT_IS_FAKE_METAL 1
#else
#define PM_METAL_PORT_IS_FAKE_METAL 0
#endif

#endif /* PM_METAL_PORT_PLATFORM_H_ */
