#include "PmBiosUefi.h"

EFI_BOOT_SERVICES *gBS;
EFI_RUNTIME_SERVICES *gRT;
EFI_HANDLE gImageHandle;
EFI_SYSTEM_TABLE_MIN *gST;

EFI_GUID gEfiPciIoProtocolGuid;
EFI_GUID gEfiGraphicsOutputProtocolGuid;
EFI_GUID gEfiMpServiceProtocolGuid;
EFI_GUID gEfiLoadedImageProtocolGuid;
EFI_GUID gEfiSimpleFileSystemProtocolGuid;
EFI_GUID gEfiFileInfoGuid;
EFI_GUID gEfiRngProtocolGuid;
EFI_GUID gEfiAbsolutePointerProtocolGuid;
EFI_GUID gEfiSimplePointerProtocolGuid;
EFI_GUID gEfiSimpleTextInputExProtocolGuid;
