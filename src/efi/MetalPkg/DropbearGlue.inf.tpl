## @file
# TEMPLATE — generated to DropbearGlue.inf by pm_metal_efi_inf_generate
# (scripts/lib/efi_inf.sh). Edit this file; the .inf is gitignored.
# dropbear_stubs -I must precede host_stubs.
##

[Defines]
  INF_VERSION                    = 0x00010005
  BASE_NAME                      = DropbearGlue
  FILE_GUID                      = A7C3E91D-4B2F-4E8A-9C1D-6F5E4A3B2C10
  MODULE_TYPE                    = BASE
  VERSION_STRING                 = 1.0
  LIBRARY_CLASS                  = DropbearGlueLib

[Sources]
  ../../pymergetic/metal/net/ssh/ssh_dropbear.c
  ../../pymergetic/metal/net/ssh/dropbear_fd.c
  ../../pymergetic/metal/net/ssh/dropbear_posix.c

[Packages]
  MdePkg/MdePkg.dec
  MetalPkg/MetalPkg.dec

[LibraryClasses]
  BaseLib
  BaseMemoryLib
  DebugLib

[BuildOptions]
  GCC:*_*_*_CC_FLAGS = -DDROPBEAR_METAL=1 -I@METAL_ROOT@/src/pymergetic/metal/net/ssh/dropbear_metal -I@METAL_ROOT@/src/pymergetic/metal/net/ssh/dropbear_stubs -I@METAL_ROOT@/external/dropbear/src -I@METAL_ROOT@/src/pymergetic/metal/net/ip -I@METAL_ROOT@/include -I@METAL_ROOT@/src/pymergetic/metal -I@METAL_ROOT@/src/pymergetic/metal/runtime/mem/host_stubs -I@METAL_ROOT@/external/lwip/src/include -I@METAL_ROOT@/external/wamr/core/iwasm/include -Wno-error -Wno-unused-parameter -Wno-sign-compare -Wno-missing-field-initializers -Wno-format -fno-strict-aliasing
