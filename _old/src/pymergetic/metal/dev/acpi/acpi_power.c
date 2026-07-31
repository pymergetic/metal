/** @file
  Minimal ACPI S5 soft-off (FADT PM1 + DSDT/SSDT \_S5_).
  Enough for real PCs where QEMU's fixed PM1 ports do nothing.
**/
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <pymergetic/metal/dev/acpi/acpi.h>
#include <pymergetic/metal/log/log.h>
#include <runtime/io/io.h>
#include <runtime/time/cpu.h>

#define ACPI_RSDP_SIG        "RSD PTR "
#define ACPI_RSDP_SIG_LEN    8u
#define ACPI_RSDP_V1_LEN     20u
#define ACPI_SLP_EN          (1u << 13)
#define ACPI_SLP_TYP_SHIFT   10u
#define ACPI_FADT_X_DSDT_OFF 140u
#define ACPI_SCI_EN          0x0001u

#pragma pack(1)
typedef struct {
  char     Sig[8];
  uint8_t  Checksum;
  char     OemId[6];
  uint8_t  Revision;
  uint32_t RsdtPaddr;
  uint32_t Length;
  uint64_t XsdtPaddr;
  uint8_t  ExtendedChecksum;
  uint8_t  Reserved[3];
} metal_acpi_rsdp_t;

typedef struct {
  char     Sig[4];
  uint32_t Length;
  uint8_t  Revision;
  uint8_t  Checksum;
  char     OemId[6];
  char     OemTableId[8];
  uint32_t OemRevision;
  char     CreatorId[4];
  uint32_t CreatorRevision;
} metal_acpi_sdt_hdr_t;

typedef struct {
  metal_acpi_sdt_hdr_t Hdr;
  uint32_t             Facs;
  uint32_t             Dsdt;
  uint8_t              Reserved0;
  uint8_t              PreferredPmProfile;
  uint16_t             SciInt;
  uint32_t             SmiCmd;
  uint8_t              AcpiEnable;
  uint8_t              AcpiDisable;
  uint8_t              S4BiosReq;
  uint8_t              PstateCnt;
  uint32_t             Pm1aEvtBlk;
  uint32_t             Pm1bEvtBlk;
  uint32_t             Pm1aCntBlk;
  uint32_t             Pm1bCntBlk;
} metal_acpi_fadt_t;
#pragma pack()

static uint8_t AcpiChecksum(const void *Base, uintptr_t Len)
{
  const uint8_t *p;
  uint8_t        sum;

  p   = (const uint8_t *)Base;
  sum = 0;
  while (Len-- > 0) {
    sum = (uint8_t)(sum + *p++);
  }

  return sum;
}

static const metal_acpi_rsdp_t *AcpiScanRange(uintptr_t Start, uintptr_t Bytes)
{
  uintptr_t Addr;
  uintptr_t End;

  End = Start + Bytes;
  for (Addr = Start & ~((uintptr_t)0xfu); Addr + ACPI_RSDP_V1_LEN <= End; Addr += 16u) {
    const metal_acpi_rsdp_t *rsdp;

    rsdp = (const metal_acpi_rsdp_t *)(uintptr_t)Addr;
    if (memcmp(rsdp->Sig, ACPI_RSDP_SIG, ACPI_RSDP_SIG_LEN) != 0) {
      continue;
    }

    if (AcpiChecksum(rsdp, ACPI_RSDP_V1_LEN) != 0) {
      continue;
    }

    return rsdp;
  }

  return NULL;
}

static const metal_acpi_rsdp_t *AcpiFindRsdp(void)
{
  const metal_acpi_rsdp_t *rsdp;
  uint16_t                 EbdaSeg;
  uintptr_t                Ebda;

  EbdaSeg = *(volatile uint16_t *)(uintptr_t)0x40E;
  if (EbdaSeg != 0) {
    Ebda = (uintptr_t)EbdaSeg << 4;
    rsdp = AcpiScanRange(Ebda, 1024u);
    if (rsdp != NULL) {
      return rsdp;
    }
  }

  return AcpiScanRange(0xE0000u, 0x20000u);
}

static const metal_acpi_sdt_hdr_t *AcpiFindTable(const metal_acpi_rsdp_t *Rsdp, const char *Sig)
{
  const metal_acpi_sdt_hdr_t *Root;
  const uint8_t              *Arr;
  uintptr_t                   EntrySize;
  uintptr_t                   Entries;
  uintptr_t                   i;

  if (Rsdp == NULL || Sig == NULL) {
    return NULL;
  }

  if (Rsdp->Revision >= 2 && Rsdp->XsdtPaddr != 0) {
    Root      = (const metal_acpi_sdt_hdr_t *)(uintptr_t)Rsdp->XsdtPaddr;
    EntrySize = sizeof(uint64_t);
  } else {
    Root      = (const metal_acpi_sdt_hdr_t *)(uintptr_t)Rsdp->RsdtPaddr;
    EntrySize = sizeof(uint32_t);
  }

  if (Root == NULL || Root->Length < sizeof(*Root)) {
    return NULL;
  }

  Entries = (Root->Length - sizeof(*Root)) / EntrySize;
  Arr     = (const uint8_t *)Root + sizeof(*Root);
  for (i = 0; i < Entries; i++) {
    uintptr_t                   Phys;
    const metal_acpi_sdt_hdr_t *Sdt;

    if (EntrySize == sizeof(uint64_t)) {
      uint64_t V;

      memcpy(&V, Arr + i * EntrySize, sizeof(V));
      Phys = (uintptr_t)V;
    } else {
      uint32_t V;

      memcpy(&V, Arr + i * EntrySize, sizeof(V));
      Phys = (uintptr_t)V;
    }

    Sdt = (const metal_acpi_sdt_hdr_t *)Phys;
    if (Sdt != NULL && memcmp(Sdt->Sig, Sig, 4) == 0) {
      return Sdt;
    }
  }

  return NULL;
}

static const metal_acpi_sdt_hdr_t *AcpiGetDsdt(const metal_acpi_fadt_t *Fadt)
{
  uintptr_t                   Phys;
  const metal_acpi_sdt_hdr_t *Dsdt;

  Phys = (uintptr_t)Fadt->Dsdt;
  if (Phys == 0 && Fadt->Hdr.Length >= ACPI_FADT_X_DSDT_OFF + 8u) {
    uint64_t XDsdt;

    memcpy(&XDsdt, (const uint8_t *)Fadt + ACPI_FADT_X_DSDT_OFF, sizeof(XDsdt));
    Phys = (uintptr_t)XDsdt;
  }

  if (Phys == 0) {
    return NULL;
  }

  Dsdt = (const metal_acpi_sdt_hdr_t *)Phys;
  if (memcmp(Dsdt->Sig, "DSDT", 4) != 0) {
    return NULL;
  }

  return Dsdt;
}

static int32_t AcpiDecodeByte(const uint8_t **pp, const uint8_t *end, uint8_t *out)
{
  const uint8_t *p;

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

static int32_t AcpiScanS5(const metal_acpi_sdt_hdr_t *Tbl, uint8_t *SlpTypA, uint8_t *SlpTypB)
{
  const uint8_t *Aml;
  uintptr_t      Len;
  uintptr_t      i;

  if (Tbl == NULL || SlpTypA == NULL || Tbl->Length < sizeof(*Tbl)) {
    return -1;
  }

  Aml = (const uint8_t *)Tbl + sizeof(*Tbl);
  Len = Tbl->Length - sizeof(*Tbl);
  for (i = 0; i + 5u < Len; i++) {
    const uint8_t *p;
    const uint8_t *end;
    uint8_t        typA;
    uint8_t        typB;

    if (memcmp(Aml + i, "_S5_", 4) != 0) {
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
    if (AcpiDecodeByte(&p, end, &typA) != 0) {
      break;
    }

    typB = typA;
    if (p < end) {
      (void)AcpiDecodeByte(&p, end, &typB);
    }

    *SlpTypA = typA;
    if (SlpTypB != NULL) {
      *SlpTypB = typB;
    }

    return 0;
  }

  return -1;
}

static int32_t AcpiFindS5(const metal_acpi_rsdp_t *Rsdp,
                          const metal_acpi_fadt_t *Fadt,
                          uint8_t                 *SlpTypA,
                          uint8_t                 *SlpTypB)
{
  const metal_acpi_sdt_hdr_t *Dsdt;
  const metal_acpi_sdt_hdr_t *Root;
  const uint8_t              *Arr;
  uintptr_t                   EntrySize;
  uintptr_t                   Entries;
  uintptr_t                   i;

  Dsdt = AcpiGetDsdt(Fadt);
  if (Dsdt != NULL && AcpiScanS5(Dsdt, SlpTypA, SlpTypB) == 0) {
    return 0;
  }

  if (Rsdp->Revision >= 2 && Rsdp->XsdtPaddr != 0) {
    Root      = (const metal_acpi_sdt_hdr_t *)(uintptr_t)Rsdp->XsdtPaddr;
    EntrySize = sizeof(uint64_t);
  } else {
    Root      = (const metal_acpi_sdt_hdr_t *)(uintptr_t)Rsdp->RsdtPaddr;
    EntrySize = sizeof(uint32_t);
  }

  if (Root == NULL || Root->Length < sizeof(*Root)) {
    return -1;
  }

  Entries = (Root->Length - sizeof(*Root)) / EntrySize;
  Arr     = (const uint8_t *)Root + sizeof(*Root);
  for (i = 0; i < Entries; i++) {
    uintptr_t                   Phys;
    const metal_acpi_sdt_hdr_t *Tbl;

    if (EntrySize == sizeof(uint64_t)) {
      uint64_t V;

      memcpy(&V, Arr + i * EntrySize, sizeof(V));
      Phys = (uintptr_t)V;
    } else {
      uint32_t V;

      memcpy(&V, Arr + i * EntrySize, sizeof(V));
      Phys = (uintptr_t)V;
    }

    Tbl = (const metal_acpi_sdt_hdr_t *)Phys;
    if (Tbl == NULL || memcmp(Tbl->Sig, "SSDT", 4) != 0) {
      continue;
    }

    if (AcpiScanS5(Tbl, SlpTypA, SlpTypB) == 0) {
      return 0;
    }
  }

  return -1;
}

static void AcpiEnable(const metal_acpi_fadt_t *Fadt)
{
  uint32_t i;

  if (Fadt->Pm1aCntBlk == 0) {
    return;
  }

  if ((pm_metal_io_in16((uint16_t)(uintptr_t)Fadt->Pm1aCntBlk) & ACPI_SCI_EN) != 0) {
    return;
  }

  if (Fadt->SmiCmd == 0 || Fadt->AcpiEnable == 0) {
    return;
  }

  pm_metal_io_out8((uint16_t)(uintptr_t)Fadt->SmiCmd, Fadt->AcpiEnable);
  for (i = 0; i < 300u; i++) {
    if ((pm_metal_io_in16((uint16_t)(uintptr_t)Fadt->Pm1aCntBlk) & ACPI_SCI_EN) != 0) {
      return;
    }

    pm_metal_cpu_pause();
  }
}

void pm_metal_acpi_poweroff(void)
{
  const metal_acpi_rsdp_t *Rsdp;
  const metal_acpi_fadt_t *Fadt;
  uint8_t                  SlpTypA;
  uint8_t                  SlpTypB;
  uint16_t                 Value;

  Rsdp = AcpiFindRsdp();
  if (Rsdp == NULL) {
    pm_metal_log("metal-acpi: no RSDP");
    return;
  }

  Fadt = (const metal_acpi_fadt_t *)AcpiFindTable(Rsdp, "FACP");
  if (Fadt == NULL || Fadt->Pm1aCntBlk == 0) {
    pm_metal_log("metal-acpi: no FADT/PM1a");
    return;
  }

  SlpTypA = 0;
  SlpTypB = 0;
  if (AcpiFindS5(Rsdp, Fadt, &SlpTypA, &SlpTypB) != 0) {
    pm_metal_log("metal-acpi: no _S5_ (trying typ=0)");
    SlpTypA = 0;
    SlpTypB = 0;
  }

  AcpiEnable(Fadt);
  Value = (uint16_t)(((uint16_t)SlpTypA << ACPI_SLP_TYP_SHIFT) | ACPI_SLP_EN);
  pm_metal_logf("metal-acpi: S5 PM1a=0x%x typ=%u", Fadt->Pm1aCntBlk, (uint32_t)SlpTypA);

  pm_metal_io_out16((uint16_t)(uintptr_t)Fadt->Pm1aCntBlk, Value);
  if (Fadt->Pm1bCntBlk != 0) {
    Value = (uint16_t)(((uint16_t)SlpTypB << ACPI_SLP_TYP_SHIFT) | ACPI_SLP_EN);
    pm_metal_io_out16((uint16_t)(uintptr_t)Fadt->Pm1bCntBlk, Value);
  }
}
