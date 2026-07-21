/** @file
  Read files from the EFI boot volume (ESP). (impl: efi)
**/
#include <pymergetic/metal/esp/esp.h>
#include <pymergetic/metal/log/log.h>
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

#define PM_METAL_ESP_CACHE_MAX  16u
#define PM_METAL_ESP_PATH_MAX   128u

typedef struct {
  INT32   used;
  CHAR8   path[PM_METAL_ESP_PATH_MAX];
  UINT8  *data;
  UINT32  len;
} metal_esp_cache_t;

STATIC metal_esp_cache_t  mCache[PM_METAL_ESP_CACHE_MAX];

STATIC
INT32
MetalEspPathEq (
  CONST CHAR8  *a,
  CONST CHAR8  *b
  )
{
  UINTN  i;

  if (a == NULL || b == NULL) {
    return 0;
  }

  for (i = 0; a[i] != '\0' && b[i] != '\0'; i++) {
    if (a[i] != b[i]) {
      return 0;
    }
  }

  return (a[i] == b[i]) ? 1 : 0;
}

STATIC
metal_esp_cache_t *
MetalEspCacheFind (
  CONST CHAR8  *path
  )
{
  UINTN  i;

  for (i = 0; i < PM_METAL_ESP_CACHE_MAX; i++) {
    if (mCache[i].used && MetalEspPathEq (mCache[i].path, path)) {
      return &mCache[i];
    }
  }

  return NULL;
}

STATIC
metal_esp_cache_t *
MetalEspCacheSlot (
  CONST CHAR8  *path
  )
{
  metal_esp_cache_t  *ent;
  UINTN               i;

  ent = MetalEspCacheFind (path);
  if (ent != NULL) {
    return ent;
  }

  for (i = 0; i < PM_METAL_ESP_CACHE_MAX; i++) {
    if (!mCache[i].used) {
      AsciiStrnCpyS (
        mCache[i].path,
        sizeof (mCache[i].path),
        path,
        sizeof (mCache[i].path) - 1
        );
      mCache[i].used = 1;
      mCache[i].data = NULL;
      mCache[i].len  = 0;
      return &mCache[i];
    }
  }

  return NULL;
}

STATIC
INT32
MetalEspCacheStore (
  CONST CHAR8   *path,
  CONST UINT8   *data,
  UINT32         len
  )
{
  metal_esp_cache_t  *ent;
  UINT8              *copy;

  ent = MetalEspCacheSlot (path);
  if (ent == NULL) {
    return -1;
  }

  copy = NULL;
  if (len > 0) {
    if (data == NULL) {
      return -1;
    }

    copy = (UINT8 *)pm_metal_mem_alloc (
                      len,
                      PM_METAL_MEM_HEAP,
                      PM_METAL_MEM_ID_NONE
                      );
    if (copy == NULL) {
      return -1;
    }

    CopyMem (copy, data, len);
  }

  if (ent->data != NULL) {
    pm_metal_mem_free (ent->data);
  }

  ent->data = copy;
  ent->len  = len;
  return 0;
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

  if (!mReady || path == NULL || File == NULL || FileSize == NULL
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

  mReady = 1;
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
pm_metal_esp_cache_put (
  CONST CHAR8   *path,
  CONST UINT8   *data,
  UINT32         len
  )
{
  if (path == NULL) {
    return -1;
  }

  return MetalEspCacheStore (path, data, len);
}

int
pm_metal_esp_preload (
  CONST CHAR8  *path
  )
{
  UINT8   *buf;
  UINT32   len;

  if (path == NULL || !mReady || gBS == NULL) {
    return -1;
  }

  buf = NULL;
  len = 0;
  if (pm_metal_esp_read_file (path, &buf, &len) != 0) {
    return -1;
  }

  if (MetalEspCacheStore (path, buf, len) != 0) {
    if (buf != NULL) {
      pm_metal_mem_free (buf);
    }

    return -1;
  }

  if (buf != NULL) {
    pm_metal_mem_free (buf);
  }

  return 0;
}

int
pm_metal_esp_file_size (
  CONST CHAR8  *path,
  UINT32       *len
  )
{
  metal_esp_cache_t  *ent;
  EFI_FILE_PROTOCOL  *File;
  UINT64              Size;

  if (len == NULL || path == NULL) {
    return -1;
  }

  *len = 0;
  ent  = MetalEspCacheFind (path);
  if (ent != NULL) {
    *len = ent->len;
    return 0;
  }

  if (gBS == NULL || !mReady) {
    return -1;
  }

  if (MetalEspOpenSized (path, &File, &Size) != 0) {
    return -1;
  }

  File->Close (File);
  *len = (UINT32)Size;
  return 0;
}

int
pm_metal_esp_read_file (
  CONST CHAR8   *path,
  UINT8        **out,
  UINT32        *len
  )
{
  metal_esp_cache_t  *ent;
  EFI_STATUS          Status;
  EFI_FILE_PROTOCOL  *File;
  UINT64              Size;
  UINT8              *Buf;
  UINTN               ReadSz;

  if (out == NULL || len == NULL || path == NULL) {
    return -1;
  }

  *out = NULL;
  *len = 0;

  ent = MetalEspCacheFind (path);
  if (ent != NULL) {
    if (ent->len == 0) {
      return 0;
    }

    Buf = (UINT8 *)pm_metal_mem_alloc (
                     ent->len,
                     PM_METAL_MEM_HEAP,
                     PM_METAL_MEM_ID_NONE
                     );
    if (Buf == NULL) {
      return -1;
    }

    CopyMem (Buf, ent->data, ent->len);
    *out = Buf;
    *len = ent->len;
    return 0;
  }

  if (gBS == NULL || !mReady) {
    return -1;
  }

  if (MetalEspOpenSized (path, &File, &Size) != 0) {
    return -1;
  }

  /* Zero-length markers are valid. */
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
pm_metal_esp_write_file (
  CONST CHAR8    *path,
  CONST UINT8    *data,
  UINT32          len
  )
{
  EFI_STATUS         Status;
  EFI_FILE_PROTOCOL  *File;
  CHAR16              Uni[256];
  UINTN               WriteSz;

  if (path == NULL) {
    return -1;
  }

  if (len > 0 && data == NULL) {
    return -1;
  }

  /* Always keep RAM cache — required after ExitBootServices. */
  if (MetalEspCacheStore (path, data, len) != 0) {
    return -1;
  }

  if (gBS == NULL || !mReady || mRoot == NULL) {
    return 0;
  }

  MetalEspAsciiToUnicode (path, Uni, sizeof (Uni) / sizeof (Uni[0]));
  if (Uni[0] == L'\0') {
    return 0;
  }

  /* Best-effort delete so Open CREATE truncates cleanly on FAT. */
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
    return 0;
  }

  WriteSz = (UINTN)len;
  if (WriteSz > 0) {
    Status = File->Write (File, &WriteSz, (VOID *)data);
    if (EFI_ERROR (Status) || WriteSz != (UINTN)len) {
      File->Close (File);
      return 0;
    }
  }

  File->Close (File);
  return 0;
}
