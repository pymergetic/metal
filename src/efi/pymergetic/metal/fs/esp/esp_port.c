/** @file
  EFI ESP port — SimpleFileSystem boot volume.
**/
#include <pymergetic/metal/fs/esp/esp.h>
#include <pymergetic/metal/log/log.h>
#include <runtime/mem/mem.h>

#include <Uefi.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleFileSystem.h>
#include <Protocol/DevicePath.h>
#include <Guid/FileInfo.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/BaseLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/DevicePathLib.h>

STATIC EFI_FILE_PROTOCOL  *mRoot;
STATIC INT32               mHwReady;

/**
 * Walk MEDIA_FILEPATH nodes into an ESP-relative ASCII path
 * (slashes, no leading separator).
 */
STATIC
INT32
MetalEspLoadedPathFromDp (
  EFI_DEVICE_PATH_PROTOCOL  *Dp,
  CHAR8                     *Out,
  UINTN                      OutCap
  )
{
  UINTN  o;

  if (Dp == NULL || Out == NULL || OutCap < 2) {
    return -1;
  }

  o = 0;
  Out[0] = '\0';
  while (!IsDevicePathEnd (Dp)) {
    if (DevicePathType (Dp) == MEDIA_DEVICE_PATH
        && DevicePathSubType (Dp) == MEDIA_FILEPATH_DP)
    {
      FILEPATH_DEVICE_PATH  *Fp;
      UINTN                  i;

      Fp = (FILEPATH_DEVICE_PATH *)Dp;
      for (i = 0; Fp->PathName[i] != L'\0'; i++) {
        CHAR16  wc;
        CHAR8   c;

        wc = Fp->PathName[i];
        if (wc > 0x7f) {
          return -1;
        }

        c = (CHAR8)wc;
        if (c == '\\') {
          c = '/';
        }

        if (c == '/' && (o == 0 || Out[o - 1] == '/')) {
          continue;
        }

        if (o + 1 >= OutCap) {
          return -1;
        }

        Out[o++] = c;
      }
    }

    Dp = NextDevicePathNode (Dp);
  }

  Out[o] = '\0';
  return (o > 0) ? 0 : -1;
}

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

/**
  Open path for read and return FileSize via GetInfo.
  On success *File is open and caller must Close; *FileSize set.
*/
STATIC
INT32
MetalEspOpenSized (
  CONST CHAR8         *path,
  EFI_FILE_PROTOCOL  **File,
  UINT64              *FileSize
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *F;
  CHAR16              Uni[256];
  UINTN               InfoSz;
  EFI_FILE_INFO      *Info;

  if (!mHwReady || path == NULL || File == NULL || FileSize == NULL
      || mRoot == NULL)
  {
    return -1;
  }

  *File     = NULL;
  *FileSize = 0;

  MetalEspAsciiToUnicode (path, Uni, sizeof (Uni) / sizeof (Uni[0]));
  if (Uni[0] == L'\0') {
    return -1;
  }

  Status = mRoot->Open (
                    mRoot,
                    &F,
                    Uni,
                    EFI_FILE_MODE_READ,
                    0
                    );
  if (EFI_ERROR (Status) || F == NULL) {
    return -1;
  }

  InfoSz = 0;
  Info   = NULL;
  Status = F->GetInfo (F, &gEfiFileInfoGuid, &InfoSz, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL || InfoSz == 0) {
    F->Close (F);
    return -1;
  }

  Info = AllocatePool (InfoSz);
  if (Info == NULL) {
    F->Close (F);
    return -1;
  }

  Status = F->GetInfo (F, &gEfiFileInfoGuid, &InfoSz, Info);
  if (EFI_ERROR (Status) || Info->FileSize > 0x2000000ULL) {
    FreePool (Info);
    F->Close (F);
    return -1;
  }

  *FileSize = Info->FileSize;
  FreePool (Info);
  *File     = F;
  return 0;
}


int
pm_metal_esp_init_port (
  VOID  *image_handle
  )
{
  EFI_STATUS                        Status;
  EFI_LOADED_IMAGE_PROTOCOL        *Loaded;
  EFI_SIMPLE_FILE_SYSTEM_PROTOCOL  *Fs;

  if (mHwReady) {
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
    pm_metal_log ("metal-esp: LoadedImage failed");
    return -1;
  }

  Status = gBS->HandleProtocol (
                  Loaded->DeviceHandle,
                  &gEfiSimpleFileSystemProtocolGuid,
                  (VOID **)&Fs
                  );
  if (EFI_ERROR (Status) || Fs == NULL) {
    pm_metal_log ("metal-esp: SimpleFileSystem failed");
    return -1;
  }

  Status = Fs->OpenVolume (Fs, &mRoot);
  if (EFI_ERROR (Status) || mRoot == NULL) {
    pm_metal_log ("metal-esp: OpenVolume failed");
    return -1;
  }

  /* Bind trust to the exact artifact that is executing (METAL-006). */
  {
    CHAR8  path[128];

    if (MetalEspLoadedPathFromDp (Loaded->FilePath, path, sizeof (path)) == 0) {
      pm_metal_esp_set_loaded_identity (
        path,
        Loaded->ImageBase,
        (UINT32)Loaded->ImageSize
        );
    } else if (Loaded->ImageBase != NULL && Loaded->ImageSize > 0) {
      pm_metal_esp_set_loaded_identity (
        "metal.efi",
        Loaded->ImageBase,
        (UINT32)Loaded->ImageSize
        );
    }
  }

  mHwReady = 1;
  return 0;
}

int
pm_metal_esp_file_size_port (
  CONST CHAR8  *path,
  UINT32       *len
  )
{
  EFI_FILE_PROTOCOL  *File;
  UINT64              Size;

  if (len == NULL || path == NULL || !mHwReady || gBS == NULL) {
    return -1;
  }

  *len = 0;
  if (MetalEspOpenSized (path, &File, &Size) != 0) {
    return -1;
  }

  File->Close (File);
  *len = (UINT32)Size;
  return 0;
}

int
pm_metal_esp_read_file_port (
  CONST CHAR8   *path,
  UINT8        **out,
  UINT32        *len
  )
{
  EFI_STATUS          Status;
  EFI_FILE_PROTOCOL  *File;
  UINT64              Size;
  UINT8              *Buf;
  UINTN               ReadSz;

  if (out == NULL || len == NULL || path == NULL || !mHwReady || gBS == NULL) {
    return -1;
  }

  *out = NULL;
  *len = 0;
  if (MetalEspOpenSized (path, &File, &Size) != 0) {
    return -1;
  }

  if (Size == 0) {
    File->Close (File);
    return 0;
  }

  ReadSz = (UINTN)Size;
  Buf    = (UINT8 *)pm_metal_mem_alloc (
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

int
pm_metal_esp_write_file_port (
  CONST CHAR8   *path,
  CONST UINT8   *data,
  UINT32         len
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *File;
  CHAR16              Uni[256];
  UINTN               WriteSz;

  if (path == NULL || !mHwReady || gBS == NULL || mRoot == NULL) {
    return -1;
  }

  if (len > 0 && data == NULL) {
    return -1;
  }

  MetalEspAsciiToUnicode (path, Uni, sizeof (Uni) / sizeof (Uni[0]));
  if (Uni[0] == L'\0') {
    return -1;
  }

  Status = mRoot->Open (
                    mRoot,
                    &File,
                    Uni,
                    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
                    0
                    );
  if (!EFI_ERROR (Status) && File != NULL) {
    (VOID)File->Delete (File);
    File = NULL;
  }

  Status = mRoot->Open (
                    mRoot,
                    &File,
                    Uni,
                    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                    0
                    );
  if (EFI_ERROR (Status) || File == NULL) {
    return -1;
  }

  WriteSz = (UINTN)len;
  if (WriteSz > 0) {
    Status = File->Write (File, &WriteSz, (VOID *)data);
    if (EFI_ERROR (Status) || WriteSz != (UINTN)len) {
      File->Close (File);
      return -1;
    }
  }

  File->Close (File);
  return 0;
}

STATIC
INT32
MetalEspQueryPath (
  CONST CHAR8  *path,
  UINT64       *file_size,
  UINT64       *attr
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *F;
  CHAR16              Uni[256];
  UINTN               InfoSz;
  EFI_FILE_INFO      *Info;

  if (!mHwReady || path == NULL || mRoot == NULL || gBS == NULL) {
    return -1;
  }

  MetalEspAsciiToUnicode (path, Uni, sizeof (Uni) / sizeof (Uni[0]));
  if (Uni[0] == L'\0') {
    return -1;
  }

  Status = mRoot->Open (
                    mRoot,
                    &F,
                    Uni,
                    EFI_FILE_MODE_READ,
                    0
                    );
  if (EFI_ERROR (Status) || F == NULL) {
    return -1;
  }

  InfoSz = 0;
  Status = F->GetInfo (F, &gEfiFileInfoGuid, &InfoSz, NULL);
  if (Status != EFI_BUFFER_TOO_SMALL || InfoSz == 0) {
    F->Close (F);
    return -1;
  }

  Info = AllocatePool (InfoSz);
  if (Info == NULL) {
    F->Close (F);
    return -1;
  }

  Status = F->GetInfo (F, &gEfiFileInfoGuid, &InfoSz, Info);
  if (EFI_ERROR (Status)) {
    FreePool (Info);
    F->Close (F);
    return -1;
  }

  if (file_size != NULL) {
    *file_size = Info->FileSize;
  }

  if (attr != NULL) {
    *attr = Info->Attribute;
  }

  FreePool (Info);
  F->Close (F);
  return 0;
}

STATIC
VOID
MetalEspUnicodeToAscii (
  CONST CHAR16  *Uni,
  CHAR8         *Ascii,
  UINTN          AsciiCap
  )
{
  UINTN  i;

  if (AsciiCap == 0) {
    return;
  }

  for (i = 0; Uni[i] != L'\0' && i + 1 < AsciiCap; i++) {
    CHAR16  c;

    c = Uni[i];
    if (c == L'\\') {
      c = L'/';
    }

    Ascii[i] = (CHAR8)(UINT16)c;
  }

  Ascii[i] = '\0';
}

int
pm_metal_esp_stat_port (
  CONST CHAR8  *path,
  UINT32       *size,
  UINT32       *type
  )
{
  UINT64  file_size;
  UINT64  attr;

  if (size == NULL || type == NULL || path == NULL) {
    return -1;
  }

  *size = 0;
  *type = PM_METAL_ESP_TYPE_FILE;
  if (MetalEspQueryPath (path, &file_size, &attr) != 0) {
    return -1;
  }

  if ((attr & EFI_FILE_DIRECTORY) != 0) {
    *type = PM_METAL_ESP_TYPE_DIR;
    return 0;
  }

  *size = (UINT32)file_size;
  return 0;
}

int
pm_metal_esp_mkdir_port (
  CONST CHAR8  *path
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *File;
  CHAR16              Uni[256];

  if (path == NULL || !mHwReady || gBS == NULL || mRoot == NULL) {
    return -1;
  }

  MetalEspAsciiToUnicode (path, Uni, sizeof (Uni) / sizeof (Uni[0]));
  if (Uni[0] == L'\0') {
    return -1;
  }

  Status = mRoot->Open (
                    mRoot,
                    &File,
                    Uni,
                    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE | EFI_FILE_MODE_CREATE,
                    EFI_FILE_DIRECTORY
                    );
  if (EFI_ERROR (Status) || File == NULL) {
    return -1;
  }

  File->Close (File);
  return 0;
}

int
pm_metal_esp_unlink_port (
  CONST CHAR8  *path
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *File;
  CHAR16              Uni[256];

  if (path == NULL || !mHwReady || gBS == NULL || mRoot == NULL) {
    return -1;
  }

  MetalEspAsciiToUnicode (path, Uni, sizeof (Uni) / sizeof (Uni[0]));
  if (Uni[0] == L'\0') {
    return -1;
  }

  Status = mRoot->Open (
                    mRoot,
                    &File,
                    Uni,
                    EFI_FILE_MODE_READ | EFI_FILE_MODE_WRITE,
                    0
                    );
  if (EFI_ERROR (Status) || File == NULL) {
    return -1;
  }

  Status = File->Delete (File);
  return EFI_ERROR (Status) ? -1 : 0;
}

int
pm_metal_esp_rename_port (
  CONST CHAR8  *old_path,
  CONST CHAR8  *new_path
  )
{
  UINT8   *data;
  UINT32   len;

  if (old_path == NULL || new_path == NULL || !mHwReady || gBS == NULL) {
    return -1;
  }

  data = NULL;
  len  = 0;
  if (pm_metal_esp_read_file_port (old_path, &data, &len) != 0) {
    return -1;
  }

  if (pm_metal_esp_write_file_port (new_path, data, len) != 0) {
    if (data != NULL) {
      pm_metal_mem_free (data);
    }

    return -1;
  }

  if (data != NULL) {
    pm_metal_mem_free (data);
  }

  return pm_metal_esp_unlink_port (old_path);
}

int
pm_metal_esp_fsync_port (
  CONST CHAR8  *path
  )
{
  (VOID)path;
  if (!mHwReady || gBS == NULL) {
    return -1;
  }

  return 0;
}

int
pm_metal_esp_readdir_port (
  CONST CHAR8  *path,
  UINT32        index,
  CHAR8        *name,
  UINT32        name_cap
  )
{
  EFI_STATUS          Status;
  EFI_FILE_PROTOCOL  *Dir;
  CHAR16              Uni[256];
  UINT32              cur;

  if (path == NULL || name == NULL || name_cap == 0 || !mHwReady || gBS == NULL
      || mRoot == NULL)
  {
    return -1;
  }

  MetalEspAsciiToUnicode (path, Uni, sizeof (Uni) / sizeof (Uni[0]));
  if (Uni[0] == L'\0') {
    return -1;
  }

  Status = mRoot->Open (
                    mRoot,
                    &Dir,
                    Uni,
                    EFI_FILE_MODE_READ,
                    0
                    );
  if (EFI_ERROR (Status) || Dir == NULL) {
    return -1;
  }

  cur = 0;
  for (;;) {
    EFI_FILE_INFO  *Info;
    CHAR8           entry[128];
    UINTN           InfoSz;

    InfoSz = SIZE_OF_EFI_FILE_INFO + 256;
    Info   = AllocatePool (InfoSz);
    if (Info == NULL) {
      Dir->Close (Dir);
      return -1;
    }

    InfoSz = SIZE_OF_EFI_FILE_INFO + 256;
    Status = Dir->Read (Dir, &InfoSz, Info);
    if (EFI_ERROR (Status) || InfoSz == 0) {
      FreePool (Info);
      break;
    }

    MetalEspUnicodeToAscii (Info->FileName, entry, sizeof (entry));
    FreePool (Info);
    if (entry[0] == '\0' || AsciiStrCmp (entry, ".") == 0
        || AsciiStrCmp (entry, "..") == 0)
    {
      continue;
    }

    if (cur == index) {
      AsciiStrnCpyS (name, name_cap, entry, name_cap - 1);
      Dir->Close (Dir);
      return 1;
    }

    cur++;
  }

  Dir->Close (Dir);
  return 0;
}
