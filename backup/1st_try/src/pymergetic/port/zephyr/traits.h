/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Zephyr port — fake metal (native_sim) vs real metal (QEMU / HW).
 * Private to src/pymergetic/port/zephyr/; not in include/.
 */

#ifndef PM_PORT_ZEPHYR_TRAITS_H_
#define PM_PORT_ZEPHYR_TRAITS_H_

#if defined(CONFIG_BOARD_NATIVE_SIM)
#define PM_METAL_PORT_IS_FAKE_METAL 1
#else
#define PM_METAL_PORT_IS_FAKE_METAL 0
#endif

#endif /* PM_PORT_ZEPHYR_TRAITS_H_ */
