/** @file
  Read files from the EFI boot volume (ESP). (impl: efi)
**/
#include <pymergetic/metal/esp.h>
#include <mem/mem.h>

#include <Uefi.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Guid/FileInfo.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/UefiLib.h>
#include <Library/MemoryAllocationLib.h>

STATIC EFI_FILE_PROTOCOL  *mRoot;
STATIC INT32               mReady;

STATIC
VOID
MetalEspAsciiToUnicode (
  CONST CHAR8  *Ascii,
  CHAR16       *Uni,
  UINTN         UniCap
  )
{
  UINTN  i;
  UINTN  o;

  o = 0;
  for (i = 0; Ascii[i] != '\0' && o + 1 < UniCap; i++) {
    CHAR8  c;

    c = Ascii[i];
    if (c == '/') {
      c = '\\';
    }

    /* Skip leading separators. */
    if (o == 0 && c == '\\') {
      continue;
    }

    Uni[o++] = (CHAR16)(UINT8)c;
  }

  Uni[o] = L'\0';
}

int
pm_metal_esp_init (
  VOID  *image_handle
  )
{
  EFI_STATUS                       Status;
  EFI_LOADED_IMAGE_PROTOCOL       *Loaded;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *Fs;

  if (mReady) {
    return 0;
  }

  if (gBS == NULL || image_handle == NULL) {
    return -1;
  }

  Status = gBS->HandleProtocol (
                  (EFI_HANDLE)image_handle,
                  &gEfiLoadedImageProtocolGuid,
                  (VOID **)&Loaded
                  );
  if (EFI_ERROR (Status) || Loaded == NULL || Loaded->DeviceHandle == NULL) {
    Print (L"metal-esp: LoadedImage failed\r\n");
    return -1;
  }

  Status = gBS->HandleProtocol (
                  Loaded->DeviceHandle,
                  &gEfiSimpleFileSystemProtocolGuid,
                  (VOID **)&Fs
                  );
  if (EFI_ERROR (Status) || Fs == NULL) {
    Print (L"metal-esp: SimpleFileSystem failed\r\n");
    return -1;
  }

  Status = Fs->OpenVolume (Fs, &mRoot);
  if (EFI_ERROR (Status) || mRoot == NULL) {
    Print (L"metal-esp: OpenVolume failed\r\n");
    return -1;
  }

  mReady = 1;
  Print (L"metal-esp: ok\r\n");
  return 0;
}

int
pm_metal_esp_ready (
  VOID
  )
{
  return mReady;
}

int
pm_metal_esp_read_file (
  CONST CHAR8   *path,
  UINT8        **out,
  UINT32        *len
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *File;
  CHAR16              Uni[256];
  UINTN               InfoSz;
  EFI_FILE_INFO      *Info;
  UINT8              *Buf;
  UINTN               ReadSz;

  if (!mReady || path == NULL || out == NULL || len == NULL || mRoot == NULL) {
    return -1;
  }

  *out = NULL;
  *len = 0;

  MetalEspAsciiToUnicode (path, Uni, sizeof (Uni) / sizeof (Uni[0]));
  if (Uni[0] == L'\0') {
    return -1;
  }

  Status = mRoot->Open (
                    mRoot,
                    &File,
                    Uni,
                    EFI_FILE_MODE_READ,
                    0
                    );
  if (EFI_ERROR (Status) || File == NULL) {
    return -1;
  }

  InfoSz = 0;
  Info   = NULL;
  Status = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSz, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL || InfoSz == 0) {
    File->Close (File);
    return -1;
  }

  Info = AllocatePool (InfoSz);
  if (Info == NULL) {
    File->Close (File);
    return -1;
  }

  Status = File->GetInfo (File, &gEfiFileInfoGuid, &InfoSz, Info);
  if (EFI_ERROR (Status) || Info->FileSize > 0x2000000ULL) {
    FreePool (Info);
    File->Close (File);
    return -1;
  }

  ReadSz = (UINTN)Info->FileSize;
  FreePool (Info);

  /* Zero-length markers (e.g. mods/apps/doom/autostart) are valid. */
  if (ReadSz == 0) {
    File->Close (File);
    *out = NULL;
    *len = 0;
    return 0;
  }

  Buf = (UINT8 *)pm_metal_mem_alloc (
                   ReadSz,
                   PM_METAL_MEM_HEAP,
                   PM_METAL_MEM_ID_NONE
                   );
  if (Buf == NULL) {
    File->Close (File);
    return -1;
  }

  Status = File->Read (File, &ReadSz, Buf);
  File->Close (File);
  if (EFI_ERROR (Status)) {
    pm_metal_mem_free (Buf);
    return -1;
  }

  *out = Buf;
  *len = (UINT32)ReadSz;
  return 0;
}
