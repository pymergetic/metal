#ifndef PM_BIOS_SHIM_INDUSTRY_STANDARD_ACPI_H_
#define PM_BIOS_SHIM_INDUSTRY_STANDARD_ACPI_H_

#ifdef __PI_UEFI_H__
#include_next <IndustryStandard/Acpi.h>
#else
#include "../PmBiosUefi.h"
#define ACPI_ADDRESS_SPACE_DESCRIPTOR 0x8A
#define ACPI_END_TAG_DESCRIPTOR 0x79
#define ACPI_ADDRESS_SPACE_TYPE_MEM 0x00
#define ACPI_ADDRESS_SPACE_TYPE_IO 0x01
typedef struct {
  UINT8 Desc;
  UINT16 Len;
  UINT8 ResType;
  UINT8 GenFlag;
  UINT8 SpecificFlag;
  UINT64 AddrSpaceGranularity;
  UINT64 AddrRangeMin;
  UINT64 AddrRangeMax;
  UINT64 AddrTranslationOffset;
  UINT64 AddrLen;
} EFI_ACPI_ADDRESS_SPACE_DESCRIPTOR;
#endif

#endif /* PM_BIOS_SHIM_INDUSTRY_STANDARD_ACPI_H_ */
