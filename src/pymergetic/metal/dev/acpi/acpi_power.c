/** @file
  Minimal ACPI S5 soft-off (FADT PM1 + DSDT/SSDT \_S5_).
  Enough for real PCs where QEMU's fixed PM1 ports do nothing.
**/
#include <pymergetic/metal/dev/acpi/acpi.h>
#include <pymergetic/metal/log/log.h>

#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/IoLib.h>

#define ACPI_RSDP_SIG        "RSD PTR "
#define ACPI_RSDP_SIG_LEN    8u
#define ACPI_RSDP_V1_LEN     20u
#define ACPI_SLP_EN          (1u << 13)
#define ACPI_SLP_TYP_SHIFT   10u
#define ACPI_FADT_X_DSDT_OFF 140u
#define ACPI_SCI_EN          0x0001u

#pragma pack (1)
typedef struct {
  CHAR8   Sig[8];
  UINT8   Checksum;
  CHAR8   OemId[6];
  UINT8   Revision;
  UINT32  RsdtPaddr;
  UINT32  Length;
  UINT64  XsdtPaddr;
  UINT8   ExtendedChecksum;
  UINT8   Reserved[3];
} metal_acpi_rsdp_t;

typedef struct {
  CHAR8   Sig[4];
  UINT32  Length;
  UINT8   Revision;
  UINT8   Checksum;
  CHAR8   OemId[6];
  CHAR8   OemTableId[8];
  UINT32  OemRevision;
  CHAR8   CreatorId[4];
  UINT32  CreatorRevision;
} metal_acpi_sdt_hdr_t;

typedef struct {
  metal_acpi_sdt_hdr_t  Hdr;
  UINT32                Facs;
  UINT32                Dsdt;
  UINT8                 Reserved0;
  UINT8                 PreferredPmProfile;
  UINT16                SciInt;
  UINT32                SmiCmd;
  UINT8                 AcpiEnable;
  UINT8                 AcpiDisable;
  UINT8                 S4BiosReq;
  UINT8                 PstateCnt;
  UINT32                Pm1aEvtBlk;
  UINT32                Pm1bEvtBlk;
  UINT32                Pm1aCntBlk;
  UINT32                Pm1bCntBlk;
} metal_acpi_fadt_t;
#pragma pack ()

STATIC
UINT8
AcpiChecksum (
  CONST VOID  *Base,
  UINTN        Len
  )
{
  CONST UINT8  *p;
  UINT8         sum;

  p   = (CONST UINT8 *)Base;
  sum = 0;
  while (Len-- > 0) {
    sum = (UINT8)(sum + *p++);
  }

  return sum;
}

STATIC
CONST metal_acpi_rsdp_t *
AcpiScanRange (
  UINTN  Start,
  UINTN  Bytes
  )
{
  UINTN  Addr;
  UINTN  End;

  End = Start + Bytes;
  for (Addr = Start & ~((UINTN)0xfu); Addr + ACPI_RSDP_V1_LEN <= End; Addr += 16u) {
    CONST metal_acpi_rsdp_t  *rsdp;

    rsdp = (CONST metal_acpi_rsdp_t *)(UINTN)Addr;
    if (CompareMem (rsdp->Sig, ACPI_RSDP_SIG, ACPI_RSDP_SIG_LEN) != 0) {
      continue;
    }

    if (AcpiChecksum (rsdp, ACPI_RSDP_V1_LEN) != 0) {
      continue;
    }

    return rsdp;
  }

  return NULL;
}

STATIC
CONST metal_acpi_rsdp_t *
AcpiFindRsdp (
  VOID
  )
{
  CONST metal_acpi_rsdp_t  *rsdp;
  UINT16                    EbdaSeg;
  UINTN                     Ebda;

  EbdaSeg = *(volatile UINT16 *)(UINTN)0x40E;
  if (EbdaSeg != 0) {
    Ebda = (UINTN)EbdaSeg << 4;
    rsdp = AcpiScanRange (Ebda, 1024u);
    if (rsdp != NULL) {
      return rsdp;
    }
  }

  return AcpiScanRange (0xE0000u, 0x20000u);
}

STATIC
CONST metal_acpi_sdt_hdr_t *
AcpiFindTable (
  CONST metal_acpi_rsdp_t  *Rsdp,
  CONST CHAR8              *Sig
  )
{
  CONST metal_acpi_sdt_hdr_t  *Root;
  CONST UINT8                 *Arr;
  UINTN                        EntrySize;
  UINTN                        Entries;
  UINTN                        i;

  if (Rsdp == NULL || Sig == NULL) {
    return NULL;
  }

  if (Rsdp->Revision >= 2 && Rsdp->XsdtPaddr != 0) {
    Root      = (CONST metal_acpi_sdt_hdr_t *)(UINTN)Rsdp->XsdtPaddr;
    EntrySize = sizeof (UINT64);
  } else {
    Root      = (CONST metal_acpi_sdt_hdr_t *)(UINTN)Rsdp->RsdtPaddr;
    EntrySize = sizeof (UINT32);
  }

  if (Root == NULL || Root->Length < sizeof (*Root)) {
    return NULL;
  }

  Entries = (Root->Length - sizeof (*Root)) / EntrySize;
  Arr     = (CONST UINT8 *)Root + sizeof (*Root);
  for (i = 0; i < Entries; i++) {
    UINTN                        Phys;
    CONST metal_acpi_sdt_hdr_t  *Sdt;

    if (EntrySize == sizeof (UINT64)) {
      UINT64  V;

      CopyMem (&V, Arr + i * EntrySize, sizeof (V));
      Phys = (UINTN)V;
    } else {
      UINT32  V;

      CopyMem (&V, Arr + i * EntrySize, sizeof (V));
      Phys = (UINTN)V;
    }

    Sdt = (CONST metal_acpi_sdt_hdr_t *)Phys;
    if (Sdt != NULL && CompareMem (Sdt->Sig, Sig, 4) == 0) {
      return Sdt;
    }
  }

  return NULL;
}

STATIC
CONST metal_acpi_sdt_hdr_t *
AcpiGetDsdt (
  CONST metal_acpi_fadt_t  *Fadt
  )
{
  UINTN                        Phys;
  CONST metal_acpi_sdt_hdr_t  *Dsdt;

  Phys = (UINTN)Fadt->Dsdt;
  if (Phys == 0 && Fadt->Hdr.Length >= ACPI_FADT_X_DSDT_OFF + 8u) {
    UINT64  XDsdt;

    CopyMem (
      &XDsdt,
      (CONST UINT8 *)Fadt + ACPI_FADT_X_DSDT_OFF,
      sizeof (XDsdt)
      );
    Phys = (UINTN)XDsdt;
  }

  if (Phys == 0) {
    return NULL;
  }

  Dsdt = (CONST metal_acpi_sdt_hdr_t *)Phys;
  if (CompareMem (Dsdt->Sig, "DSDT", 4) != 0) {
    return NULL;
  }

  return Dsdt;
}

STATIC
INT32
AcpiDecodeByte (
  CONST UINT8  **pp,
  CONST UINT8   *end,
  UINT8         *out
  )
{
  CONST UINT8  *p;

  p = *pp;
  if (p >= end) {
    return -1;
  }

  if (*p == 0x0Au) {
    if (p + 1 >= end) {
      return -1;
    }

    *out = p[1];
    *pp  = p + 2;
    return 0;
  }

  *out = *p;
  *pp  = p + 1;
  return 0;
}

STATIC
INT32
AcpiScanS5 (
  CONST metal_acpi_sdt_hdr_t  *Tbl,
  UINT8                       *SlpTypA,
  UINT8                       *SlpTypB
  )
{
  CONST UINT8  *Aml;
  UINTN         Len;
  UINTN         i;

  if (Tbl == NULL || SlpTypA == NULL || Tbl->Length < sizeof (*Tbl)) {
    return -1;
  }

  Aml = (CONST UINT8 *)Tbl + sizeof (*Tbl);
  Len = Tbl->Length - sizeof (*Tbl);
  for (i = 0; i + 5u < Len; i++) {
    CONST UINT8  *p;
    CONST UINT8  *end;
    UINT8         typA;
    UINT8         typB;

    if (CompareMem (Aml + i, "_S5_", 4) != 0) {
      continue;
    }

    p   = Aml + i + 4;
    end = Aml + Len;
    if (p >= end || *p != 0x12u) {
      continue;
    }

    p++;
    if (p >= end) {
      break;
    }

    p += 1u + (*p >> 6);
    if (p >= end) {
      break;
    }

    p++;
    if (AcpiDecodeByte (&p, end, &typA) != 0) {
      break;
    }

    typB = typA;
    if (p < end) {
      (VOID)AcpiDecodeByte (&p, end, &typB);
    }

    *SlpTypA = typA;
    if (SlpTypB != NULL) {
      *SlpTypB = typB;
    }

    return 0;
  }

  return -1;
}

STATIC
INT32
AcpiFindS5 (
  CONST metal_acpi_rsdp_t  *Rsdp,
  CONST metal_acpi_fadt_t  *Fadt,
  UINT8                    *SlpTypA,
  UINT8                    *SlpTypB
  )
{
  CONST metal_acpi_sdt_hdr_t  *Dsdt;
  CONST metal_acpi_sdt_hdr_t  *Root;
  CONST UINT8                 *Arr;
  UINTN                        EntrySize;
  UINTN                        Entries;
  UINTN                        i;

  Dsdt = AcpiGetDsdt (Fadt);
  if (Dsdt != NULL && AcpiScanS5 (Dsdt, SlpTypA, SlpTypB) == 0) {
    return 0;
  }

  if (Rsdp->Revision >= 2 && Rsdp->XsdtPaddr != 0) {
    Root      = (CONST metal_acpi_sdt_hdr_t *)(UINTN)Rsdp->XsdtPaddr;
    EntrySize = sizeof (UINT64);
  } else {
    Root      = (CONST metal_acpi_sdt_hdr_t *)(UINTN)Rsdp->RsdtPaddr;
    EntrySize = sizeof (UINT32);
  }

  if (Root == NULL || Root->Length < sizeof (*Root)) {
    return -1;
  }

  Entries = (Root->Length - sizeof (*Root)) / EntrySize;
  Arr     = (CONST UINT8 *)Root + sizeof (*Root);
  for (i = 0; i < Entries; i++) {
    UINTN                        Phys;
    CONST metal_acpi_sdt_hdr_t  *Tbl;

    if (EntrySize == sizeof (UINT64)) {
      UINT64  V;

      CopyMem (&V, Arr + i * EntrySize, sizeof (V));
      Phys = (UINTN)V;
    } else {
      UINT32  V;

      CopyMem (&V, Arr + i * EntrySize, sizeof (V));
      Phys = (UINTN)V;
    }

    Tbl = (CONST metal_acpi_sdt_hdr_t *)Phys;
    if (Tbl == NULL || CompareMem (Tbl->Sig, "SSDT", 4) != 0) {
      continue;
    }

    if (AcpiScanS5 (Tbl, SlpTypA, SlpTypB) == 0) {
      return 0;
    }
  }

  return -1;
}

STATIC
VOID
AcpiEnable (
  CONST metal_acpi_fadt_t  *Fadt
  )
{
  UINT32  i;

  if (Fadt->Pm1aCntBlk == 0) {
    return;
  }

  if ((IoRead16 ((UINTN)Fadt->Pm1aCntBlk) & ACPI_SCI_EN) != 0) {
    return;
  }

  if (Fadt->SmiCmd == 0 || Fadt->AcpiEnable == 0) {
    return;
  }

  IoWrite8 ((UINTN)Fadt->SmiCmd, Fadt->AcpiEnable);
  for (i = 0; i < 300u; i++) {
    if ((IoRead16 ((UINTN)Fadt->Pm1aCntBlk) & ACPI_SCI_EN) != 0) {
      return;
    }

    CpuPause ();
  }
}

void
pm_metal_acpi_poweroff (
  VOID
  )
{
  CONST metal_acpi_rsdp_t  *Rsdp;
  CONST metal_acpi_fadt_t  *Fadt;
  UINT8                     SlpTypA;
  UINT8                     SlpTypB;
  UINT16                    Value;

  Rsdp = AcpiFindRsdp ();
  if (Rsdp == NULL) {
    pm_metal_log ("metal-acpi: no RSDP");
    return;
  }

  Fadt = (CONST metal_acpi_fadt_t *)AcpiFindTable (Rsdp, "FACP");
  if (Fadt == NULL || Fadt->Pm1aCntBlk == 0) {
    pm_metal_log ("metal-acpi: no FADT/PM1a");
    return;
  }

  SlpTypA = 0;
  SlpTypB = 0;
  if (AcpiFindS5 (Rsdp, Fadt, &SlpTypA, &SlpTypB) != 0) {
    pm_metal_log ("metal-acpi: no _S5_ (trying typ=0)");
    SlpTypA = 0;
    SlpTypB = 0;
  }

  AcpiEnable (Fadt);
  Value = (UINT16)(((UINT16)SlpTypA << ACPI_SLP_TYP_SHIFT) | ACPI_SLP_EN);
  pm_metal_logf (
    "metal-acpi: S5 PM1a=0x%x typ=%u",
    Fadt->Pm1aCntBlk,
    (UINT32)SlpTypA
    );

  IoWrite16 ((UINTN)Fadt->Pm1aCntBlk, Value);
  if (Fadt->Pm1bCntBlk != 0) {
    Value = (UINT16)(((UINT16)SlpTypB << ACPI_SLP_TYP_SHIFT) | ACPI_SLP_EN);
    IoWrite16 ((UINTN)Fadt->Pm1bCntBlk, Value);
  }
}
