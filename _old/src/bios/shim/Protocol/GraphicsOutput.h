#ifndef PM_BIOS_SHIM_PROTOCOL_GRAPHICS_OUTPUT_H_
#define PM_BIOS_SHIM_PROTOCOL_GRAPHICS_OUTPUT_H_

/* EDK2 <Uefi.h> already open — use the real MdePkg header next on -I. */
#ifdef __PI_UEFI_H__
#include_next <Protocol/GraphicsOutput.h>
#else
#include "../PmBiosUefi.h"
typedef struct {
  UINT32 RedMask;
  UINT32 GreenMask;
  UINT32 BlueMask;
  UINT32 ReservedMask;
} EFI_PIXEL_BITMASK;
typedef enum {
  PixelRedGreenBlueReserved8BitPerColor,
  PixelBlueGreenRedReserved8BitPerColor,
  PixelBitMask,
  PixelBltOnly,
  PixelFormatMax
} EFI_GRAPHICS_PIXEL_FORMAT;
typedef struct {
  UINT32 Version;
  UINT32 HorizontalResolution;
  UINT32 VerticalResolution;
  EFI_GRAPHICS_PIXEL_FORMAT PixelFormat;
  EFI_PIXEL_BITMASK PixelInformation;
  UINT32 PixelsPerScanLine;
} EFI_GRAPHICS_OUTPUT_MODE_INFORMATION;
typedef struct {
  UINT32 MaxMode;
  UINT32 Mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *Info;
  UINTN SizeOfInfo;
  EFI_PHYSICAL_ADDRESS FrameBufferBase;
  UINTN FrameBufferSize;
} EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE;
typedef struct {
  UINT8 Blue;
  UINT8 Green;
  UINT8 Red;
  UINT8 Reserved;
} EFI_GRAPHICS_OUTPUT_BLT_PIXEL;
typedef enum {
  EfiBltVideoFill,
  EfiBltVideoToBltBuffer,
  EfiBltBufferToVideo,
  EfiBltVideoToVideo,
  EfiGraphicsOutputBltOperationMax
} EFI_GRAPHICS_OUTPUT_BLT_OPERATION;
typedef struct _EFI_GRAPHICS_OUTPUT_PROTOCOL EFI_GRAPHICS_OUTPUT_PROTOCOL;
struct _EFI_GRAPHICS_OUTPUT_PROTOCOL {
  EFI_STATUS (*QueryMode)(EFI_GRAPHICS_OUTPUT_PROTOCOL *This, UINT32 ModeNumber,
			  UINTN *SizeOfInfo,
			  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info);
  EFI_STATUS (*SetMode)(EFI_GRAPHICS_OUTPUT_PROTOCOL *This, UINT32 ModeNumber);
  EFI_STATUS (*Blt)(EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
		    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *BltBuffer,
		    EFI_GRAPHICS_OUTPUT_BLT_OPERATION BltOperation,
		    UINTN SourceX, UINTN SourceY, UINTN DestinationX,
		    UINTN DestinationY, UINTN Width, UINTN Height, UINTN Delta);
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE *Mode;
};
extern EFI_GUID gEfiGraphicsOutputProtocolGuid;
#endif /* __PI_UEFI_H__ */

#endif /* PM_BIOS_SHIM_PROTOCOL_GRAPHICS_OUTPUT_H_ */
