/*
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef PM_PORT_ZEPHYR_EFI_RAM_H_
#define PM_PORT_ZEPHYR_EFI_RAM_H_

#include <stddef.h>

typedef enum {
	PM_PORT_EFI_RAM_SOURCE_NONE = 0,
	PM_PORT_EFI_RAM_SOURCE_MEMMAP,
	PM_PORT_EFI_RAM_SOURCE_SMBIOS,
} pm_port_efi_ram_source_t;

size_t pm_port_efi_machine_ram(void);
pm_port_efi_ram_source_t pm_port_efi_ram_source(void);

#endif /* PM_PORT_ZEPHYR_EFI_RAM_H_ */
