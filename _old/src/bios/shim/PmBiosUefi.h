/* Freestanding UEFI-shaped types for Metal BIOS (not EDK2).
 *
 * If EDK2 MdePkg <Uefi.h> is already open (__PI_UEFI_H__), stay out of the
 * way — BIOS Protocol/Library shims may be found first on a merged -I path.
 */
#ifndef PM_BIOS_SHIM_UEFI_H_
#define PM_BIOS_SHIM_UEFI_H_

#ifndef __PI_UEFI_H__

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void VOID;
typedef uint8_t UINT8;
typedef uint16_t UINT16;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef int8_t INT8;
typedef int16_t INT16;
typedef int32_t INT32;
typedef int64_t INT64;
typedef uintptr_t UINTN;
typedef intptr_t INTN;
typedef UINT8 BOOLEAN;
typedef char CHAR8;
typedef uint16_t CHAR16;
typedef UINTN EFI_STATUS;
typedef VOID *EFI_HANDLE;
typedef VOID *EFI_EVENT;
typedef UINT64 EFI_PHYSICAL_ADDRESS;
typedef UINT64 EFI_VIRTUAL_ADDRESS;

typedef struct {
  UINT32 Data1;
  UINT16 Data2;
  UINT16 Data3;
  UINT8 Data4[8];
} EFI_GUID;

#ifndef NULL
#define NULL ((VOID *)0)
#endif

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#ifndef CONST
#define CONST const
#endif
#ifndef STATIC
#define STATIC static
#endif
#ifndef IN
#define IN
#endif
#ifndef OUT
#define OUT
#endif
#ifndef OPTIONAL
#define OPTIONAL
#endif
#ifndef EFIAPI
#define EFIAPI
#endif

#ifndef EFI_SUCCESS
#define EFI_SUCCESS 0u
#endif
#ifndef EFI_LOAD_ERROR
#define EFI_LOAD_ERROR 1u
#endif
#ifndef EFI_INVALID_PARAMETER
#define EFI_INVALID_PARAMETER 2u
#endif
#ifndef EFI_UNSUPPORTED
#define EFI_UNSUPPORTED 3u
#endif
#ifndef EFI_BAD_BUFFER_SIZE
#define EFI_BAD_BUFFER_SIZE 4u
#endif
#ifndef EFI_BUFFER_TOO_SMALL
#define EFI_BUFFER_TOO_SMALL 5u
#endif
#ifndef EFI_NOT_READY
#define EFI_NOT_READY 6u
#endif
#ifndef EFI_DEVICE_ERROR
#define EFI_DEVICE_ERROR 7u
#endif
#ifndef EFI_WRITE_PROTECTED
#define EFI_WRITE_PROTECTED 8u
#endif
#ifndef EFI_OUT_OF_RESOURCES
#define EFI_OUT_OF_RESOURCES 9u
#endif
#ifndef EFI_NOT_FOUND
#define EFI_NOT_FOUND 14u
#endif

#ifndef EFI_ERROR
#define EFI_ERROR(Status) ((INTN)(UINTN)(Status) < 0 || (Status) != EFI_SUCCESS)
#endif

typedef UINTN RETURN_STATUS;
#ifndef RETURN_SUCCESS
#define RETURN_SUCCESS EFI_SUCCESS
#endif

#define EFI_PAGE_SIZE 4096u
#define EFI_PAGE_MASK (EFI_PAGE_SIZE - 1u)
#define EFI_SIZE_TO_PAGES(Size) \
  (((UINTN)(Size) + EFI_PAGE_MASK) / EFI_PAGE_SIZE)

#ifndef OFFSET_OF
#define OFFSET_OF(TYPE, Field) ((UINTN) & (((TYPE *)0)->Field))
#endif

typedef va_list VA_LIST;
#define VA_START(a, b) va_start(a, b)
#define VA_END(a) va_end(a)
#define VA_ARG(a, t) va_arg(a, t)
#define VA_COPY(d, s) va_copy(d, s)

typedef struct EFI_BOOT_SERVICES EFI_BOOT_SERVICES;
typedef struct EFI_RUNTIME_SERVICES EFI_RUNTIME_SERVICES;
typedef struct EFI_SYSTEM_TABLE EFI_SYSTEM_TABLE;

extern EFI_BOOT_SERVICES *gBS;
extern EFI_RUNTIME_SERVICES *gRT;
extern EFI_HANDLE gImageHandle;

#define CHAR_CARRIAGE_RETURN 0x000D
#define CHAR_BACKSPACE 0x0008
#define CHAR_LINEFEED 0x000A
#define CHAR_TAB 0x0009

typedef struct {
  UINT16 ScanCode;
  CHAR16 UnicodeChar;
} EFI_INPUT_KEY;

typedef struct {
  VOID *ConIn;
  VOID *ConOut;
  VOID *ConsoleInHandle;
} EFI_SYSTEM_TABLE_MIN;

extern EFI_SYSTEM_TABLE_MIN *gST;

struct EFI_BOOT_SERVICES {
  EFI_STATUS (*Stall)(UINTN Microseconds);
};

#ifdef __cplusplus
}
#endif

#endif /* !__PI_UEFI_H__ */
#endif /* PM_BIOS_SHIM_UEFI_H_ */
