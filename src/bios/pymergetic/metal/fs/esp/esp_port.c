/** @file
  BIOS ESP port — no SimpleFileSystem; cache-only via shared.
**/

#include <pymergetic/metal/fs/esp/esp.h>
#include <Uefi.h>

int
pm_metal_esp_init_port (
  VOID  *image_handle
  )
{
  (VOID)image_handle;
  return 0;
}

int
pm_metal_esp_read_file_port (
  CONST CHAR8   *path,
  UINT8        **out,
  UINT32        *len
  )
{
  (VOID)path;
  if (out != NULL) {
    *out = NULL;
  }

  if (len != NULL) {
    *len = 0;
  }

  return -1;
}

int
pm_metal_esp_write_file_port (
  CONST CHAR8   *path,
  CONST UINT8   *data,
  UINT32         len
  )
{
  (VOID)path;
  (VOID)data;
  (VOID)len;
  return -1;
}

int
pm_metal_esp_file_size_port (
  CONST CHAR8  *path,
  UINT32       *len
  )
{
  (VOID)path;
  if (len != NULL) {
    *len = 0;
  }

  return -1;
}

int
pm_metal_esp_stat_port (
  CONST CHAR8  *path,
  UINT32       *size,
  UINT32       *type
  )
{
  (VOID)path;
  if (size != NULL) {
    *size = 0;
  }

  if (type != NULL) {
    *type = PM_METAL_ESP_TYPE_FILE;
  }

  return -1;
}

int
pm_metal_esp_mkdir_port (
  CONST CHAR8  *path
  )
{
  (VOID)path;
  return -1;
}

int
pm_metal_esp_unlink_port (
  CONST CHAR8  *path
  )
{
  (VOID)path;
  return -1;
}

int
pm_metal_esp_rename_port (
  CONST CHAR8  *old_path,
  CONST CHAR8  *new_path
  )
{
  (VOID)old_path;
  (VOID)new_path;
  return -1;
}

int
pm_metal_esp_fsync_port (
  CONST CHAR8  *path
  )
{
  (VOID)path;
  return -1;
}

int
pm_metal_esp_readdir_port (
  CONST CHAR8  *path,
  UINT32        index,
  CHAR8        *name,
  UINT32        name_cap
  )
{
  (VOID)path;
  (VOID)index;
  (VOID)name;
  (VOID)name_cap;
  return -1;
}
