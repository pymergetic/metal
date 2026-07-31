#ifndef PM_BIOS_SHIM_PROTOCOL_PCI_IO_H_
#define PM_BIOS_SHIM_PROTOCOL_PCI_IO_H_

/* EDK2 <Uefi.h> already open — use the real MdePkg header next on -I. */
#ifdef __PI_UEFI_H__
#include_next <Protocol/PciIo.h>
#else
#include "../PmBiosUefi.h"
typedef struct _EFI_PCI_IO_PROTOCOL EFI_PCI_IO_PROTOCOL;
extern EFI_GUID gEfiPciIoProtocolGuid;
#endif /* __PI_UEFI_H__ */

#endif /* PM_BIOS_SHIM_PROTOCOL_PCI_IO_H_ */
