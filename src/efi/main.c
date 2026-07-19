/*
 * Freestanding UEFI entry — scaffold only.
 * Full ExitBootServices + WAMR bring-up lands with the virtio stubs.
 */
#include <stdint.h>

/* UEFI EFIAPI calling convention on x86_64. */
#if defined(__x86_64__) || defined(_M_X64)
#define EFIAPI __attribute__((ms_abi))
#else
#define EFIAPI
#endif

typedef uint64_t EFI_STATUS;
typedef uint16_t CHAR16;
typedef void *EFI_HANDLE;
typedef struct EFI_SYSTEM_TABLE EFI_SYSTEM_TABLE;

#define EFI_SUCCESS 0

EFI_STATUS EFIAPI efi_main(EFI_HANDLE image, EFI_SYSTEM_TABLE *st)
{
	(void)image;
	(void)st;
	/* TODO: locate GOP/Serial, ExitBootServices, pm_metal_efi_run(). */
	return EFI_SUCCESS;
}
