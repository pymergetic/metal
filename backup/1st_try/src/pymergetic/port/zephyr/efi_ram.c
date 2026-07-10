/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * EFI installed-RAM probe (pymergetic port only — vanilla Zephyr unchanged).
 *
 * Primary: EFI Boot Services GetMemoryMap via CR3 swap + MS-ABI thunk.
 * Fallback: SMBIOS from the EFI configuration table (efi_systab + efi_cr3).
 */

#include "../headers/efi_ram.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/kernel/mm.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_X86_EFI)

#include <zephyr/arch/x86/efi.h>
#include <efi.h>

extern struct efi_boot_arg *efi;

#if defined(CONFIG_MMU_PAGE_SIZE)
#define PM_PHYS_PAGE_SIZE CONFIG_MMU_PAGE_SIZE
#else
#define PM_PHYS_PAGE_SIZE 4096
#endif

#define PM_EFI_PAGE_SIZE 4096U

static void pm_efi_swap_cr3(uintptr_t cr3)
{
	__asm__ volatile("movq %0, %%cr3" :: "r"(cr3) : "memory");
}

static uint64_t __aligned(64) pm_efi_thunk_stack[1024];
static uint8_t pm_efi_mmap_storage[16384] __aligned(8);

static efi_get_memory_map_t pm_efi_get_mmap_fn;
static uintptr_t pm_efi_mmap_size;
static struct efi_memory_descriptor *pm_efi_mmap_buf;
static uintptr_t pm_efi_mmap_key;
static uintptr_t pm_efi_mmap_desc_size;
static uint32_t pm_efi_mmap_desc_version;

static efi_status_t pm_efi_get_memory_map_call(uintptr_t efi_cr3, efi_get_memory_map_t fn)
{
	void *stack_base = &pm_efi_thunk_stack[ARRAY_SIZE(pm_efi_thunk_stack) - 8];
	struct {
		uintptr_t *map_size;
		struct efi_memory_descriptor *map;
		uintptr_t *map_key;
		uintptr_t *desc_size;
		uint32_t *desc_version;
	} args = {
		.map_size = &pm_efi_mmap_size,
		.map = pm_efi_mmap_buf,
		.map_key = &pm_efi_mmap_key,
		.desc_size = &pm_efi_mmap_desc_size,
		.desc_version = &pm_efi_mmap_desc_version,
	};
	efi_status_t status;

	k_sched_lock();

	__asm__ volatile("movq %%cr3, %%r12;"
			 "movq %%rsp, %%r13;"
			 "movq %[stack], %%rsp;"
			 "andq $-16, %%rsp;"
			 "subq $40, %%rsp;"
			 "movq 32(%[args]), %%r10;"
			 "movq %%r10, 32(%%rsp);"
			 "movq (%[args]), %%rcx;"
			 "movq 8(%[args]), %%rdx;"
			 "movq 16(%[args]), %%r8;"
			 "movq 24(%[args]), %%r9;"
			 "movq %[fn], %%rax;"
			 "movq %[cr3], %%rdi;"
			 "movq %%rdi, %%cr3;"
			 "call *%%rax;"
			 "cli;"
			 "movq %%r12, %%cr3;"
			 "movq %%r13, %%rsp;"
			 : "=a"(status)
			 : [args] "r"(&args), [fn] "r"(fn), [cr3] "r"(efi_cr3), [stack] "r"(stack_base)
			 : "rcx", "rdx", "r8", "r9", "r10", "rdi", "r12", "r13", "memory",
			   "cc");

	k_sched_unlock();
	return status;
}

static size_t pm_efi_memmap_probe_once(void)
{
	ARG_UNUSED(pm_efi_get_memory_map_call);
	return 0U;
}

#if 0 /* Runtime GetMemoryMap via Boot Services faults under OVMF+Zephyr CR3 today. */
static size_t pm_efi_memmap_probe_once_live(void)
{
	uintptr_t z_cr3 = 0U;
	uintptr_t efi_cr3;
	struct efi_system_table *st;
	struct efi_boot_services *bs;
	efi_status_t status;
	uint8_t *map_buf = NULL;
	size_t total = 0U;

	if (efi == NULL || efi->efi_systab == NULL || efi->efi_cr3 == 0ULL) {
		return 0U;
	}

	efi_cr3 = (uintptr_t)efi->efi_cr3;
	st = efi->efi_systab;

	__asm__ volatile("movq %%cr3, %0" : "=r"(z_cr3));
	pm_efi_swap_cr3(efi_cr3);
	bs = st->BootServices;
	pm_efi_swap_cr3(z_cr3);

	if (bs == NULL || bs->GetMemoryMap == NULL) {
		return 0U;
	}

	pm_efi_get_mmap_fn = bs->GetMemoryMap;
	pm_efi_mmap_size = 0U;
	pm_efi_mmap_buf = NULL;
	pm_efi_mmap_key = 0U;
	pm_efi_mmap_desc_size = 0U;
	pm_efi_mmap_desc_version = 0U;

	status = pm_efi_get_memory_map_call(efi_cr3, pm_efi_get_mmap_fn);
	if (status != EFI_BUFFER_TOO_SMALL || pm_efi_mmap_size == 0U || pm_efi_mmap_desc_size == 0U) {
		return 0U;
	}

	pm_efi_mmap_size += pm_efi_mmap_desc_size;

	if (pm_efi_mmap_size > sizeof(pm_efi_mmap_storage)) {
		return 0U;
	}

	map_buf = pm_efi_mmap_storage;
	pm_efi_mmap_buf = (struct efi_memory_descriptor *)map_buf;
	status = pm_efi_get_memory_map_call(efi_cr3, pm_efi_get_mmap_fn);
	if (status != EFI_SUCCESS) {
		return 0U;
	}

	for (uintptr_t off = 0U; off < pm_efi_mmap_size; off += pm_efi_mmap_desc_size) {
		const struct efi_memory_descriptor *desc =
			(const struct efi_memory_descriptor *)(map_buf + off);

		if (desc->Type == EfiConventionalMemory) {
			total += (size_t)desc->NumberOfPages * PM_EFI_PAGE_SIZE;
		}
	}

	return total;
}
#endif

#define PM_SMBIOS_TYPE_PHYSICAL_MEMORY_ARRAY 16U
#define PM_SMBIOS_TYPE_MEMORY_DEVICE         17U
#define PM_SMBIOS_TYPE_MEMORY_MAPPED_ADDR    19U

struct pm_smbios_eps {
	char anchor[4];
	uint8_t checksum;
	uint8_t length;
	uint8_t major;
	uint8_t minor;
	uint16_t max_struct_size;
	uint8_t revision;
	char int_anchor[5];
	uint8_t int_checksum;
	uint16_t table_length;
	uint32_t table_address;
	uint16_t num_structures;
	uint8_t bcd_revision;
} __attribute__((packed));

struct pm_smbios3_eps {
	char anchor[5];
	uint8_t checksum;
	uint8_t length;
	uint8_t major;
	uint8_t minor;
	uint8_t docrev;
	uint8_t revision;
	uint8_t reserved;
	uint32_t table_max_size;
	uint64_t table_address;
} __attribute__((packed));

struct pm_smbios_hdr {
	uint8_t type;
	uint8_t length;
	uint16_t handle;
} __attribute__((packed));

typedef struct {
	uint32_t data1;
	uint16_t data2;
	uint16_t data3;
	uint8_t data4[8];
} pm_efi_guid_t;

typedef struct {
	pm_efi_guid_t vendor_guid;
	void *vendor_table;
} pm_efi_config_table_t;

struct pm_efi_table_header {
	uint64_t signature;
	uint32_t revision;
	uint32_t header_size;
	uint32_t crc32;
	uint32_t reserved;
};

struct pm_efi_system_table {
	struct pm_efi_table_header hdr;
	uint16_t *firmware_vendor;
	uint32_t firmware_revision;
	void *console_in_handle;
	void *con_in;
	void *console_out_handle;
	void *con_out;
	void *standard_error_handle;
	void *std_err;
	void *runtime_services;
	void *boot_services;
	uint64_t number_of_table_entries;
	pm_efi_config_table_t *configuration_table;
};

static const pm_efi_guid_t pm_efi_smbios3_guid = {
	.data1 = 0xf2fd1544U,
	.data2 = 0x9794U,
	.data3 = 0x4a2cU,
	.data4 = {0x99, 0x2e, 0xe5, 0xbb, 0xcf, 0x20, 0xe3, 0x94},
};

static const pm_efi_guid_t pm_efi_smbios_guid = {
	.data1 = 0xeb9d2d31U,
	.data2 = 0x2d88U,
	.data3 = 0x11d3U,
	.data4 = {0x9a, 0x16, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d},
};

static uint8_t pm_smbios_checksum(const void *data, size_t len)
{
	const uint8_t *bytes = data;
	uint8_t sum = 0U;

	for (size_t i = 0U; i < len; i++) {
		sum += bytes[i];
	}

	return sum;
}

static bool pm_efi_guid_eq(const pm_efi_guid_t *left, const pm_efi_guid_t *right)
{
	uint64_t left_parts[2];
	uint64_t right_parts[2];

	memcpy(left_parts, left, sizeof(left_parts));
	memcpy(right_parts, right, sizeof(right_parts));

	return left_parts[0] == right_parts[0] && left_parts[1] == right_parts[1];
}

static int pm_phys_copy(void *dst, uintptr_t phys, size_t len)
{
	uint8_t *virt = NULL;
	uintptr_t map_base = ROUND_DOWN(phys, (uintptr_t)PM_PHYS_PAGE_SIZE);
	size_t map_size = ROUND_UP((phys - map_base) + len, PM_PHYS_PAGE_SIZE);

	k_mem_map_phys_bare(&virt, map_base, map_size, K_MEM_PERM_RW);
	if (virt == NULL) {
		return -ENOMEM;
	}

	memcpy(dst, virt + (phys - map_base), len);
	k_mem_unmap_phys_bare(virt, map_size);
	return 0;
}

static void *pm_efi_lookup_vendor_table(const pm_efi_guid_t *want)
{
	uintptr_t z_cr3 = 0U;
	uintptr_t efi_cr3;
	struct pm_efi_system_table *st;
	void *vendor = NULL;

	if (efi == NULL || efi->efi_systab == NULL || efi->efi_cr3 == 0ULL) {
		return NULL;
	}

	/* Handoff fields live in Zephyr-mapped memory — read before CR3 swap. */
	efi_cr3 = (uintptr_t)efi->efi_cr3;
	st = efi->efi_systab;

	__asm__ volatile("movq %%cr3, %0" : "=r"(z_cr3));
	pm_efi_swap_cr3(efi_cr3);

	for (uint64_t i = 0U; i < st->number_of_table_entries; i++) {
		pm_efi_config_table_t entry;

		memcpy(&entry, &st->configuration_table[i], sizeof(entry));
		if (pm_efi_guid_eq(&entry.vendor_guid, want)) {
			vendor = entry.vendor_table;
			break;
		}
	}

	pm_efi_swap_cr3(z_cr3);
	return vendor;
}

static void *pm_efi_copy_vendor_view(void *vendor, void *buf, size_t len)
{
	uintptr_t z_cr3 = 0U;
	uintptr_t efi_cr3;

	if (vendor == NULL || buf == NULL || len == 0U || efi == NULL || efi->efi_cr3 == 0ULL) {
		return NULL;
	}

	efi_cr3 = (uintptr_t)efi->efi_cr3;

	__asm__ volatile("movq %%cr3, %0" : "=r"(z_cr3));
	pm_efi_swap_cr3(efi_cr3);
	memcpy(buf, vendor, len);
	pm_efi_swap_cr3(z_cr3);
	return buf;
}

static size_t pm_smbios_type17_bytes(const struct pm_smbios_hdr *hdr)
{
	const uint8_t *raw = (const uint8_t *)hdr;

	if (hdr->length < 0x1CU) {
		return 0U;
	}

	const uint16_t size = (uint16_t)raw[0x0C] | ((uint16_t)raw[0x0D] << 8);

	if (size == 0U) {
		return 0U;
	}
	if (size == 0x7FFFU) {
		const uint32_t ext_mb = (uint32_t)raw[0x1C] | ((uint32_t)raw[0x1D] << 8) |
					((uint32_t)raw[0x1E] << 16) | ((uint32_t)raw[0x1F] << 24);

		return (size_t)ext_mb * 1024U * 1024U;
	}
	if ((size & 0x8000U) != 0U) {
		return (size_t)(size & 0x7FFFU) * 1024U * 1024U;
	}

	/* OVMF/QEMU often encodes DIMM size in MiB without setting bit 15. */
	if (size >= 128U && size <= 8192U && (size & (size - 1U)) == 0U) {
		return (size_t)size * 1024U * 1024U;
	}

	return (size_t)size * 1024U;
}

static size_t pm_smbios_type16_bytes(const struct pm_smbios_hdr *hdr)
{
	const uint8_t *raw = (const uint8_t *)hdr;

	if (hdr->length < 0x17U) {
		return 0U;
	}

	const uint32_t max_cap = (uint32_t)raw[0x0F] | ((uint32_t)raw[0x10] << 8) |
				 ((uint32_t)raw[0x11] << 16) | ((uint32_t)raw[0x12] << 24);

	if (hdr->length >= 0x1CU) {
		const uint64_t ext = (uint64_t)raw[0x17] | ((uint64_t)raw[0x18] << 8) |
				     ((uint64_t)raw[0x19] << 16) | ((uint64_t)raw[0x1A] << 24) |
				     ((uint64_t)raw[0x1B] << 32) | ((uint64_t)raw[0x1C] << 40) |
				     ((uint64_t)raw[0x1D] << 48) | ((uint64_t)raw[0x1E] << 56);

		if (ext != 0ULL) {
			return (size_t)ext;
		}
	}

	if (max_cap == 0x80000000U) {
		return 0U;
	}

	return (size_t)max_cap * 1024U;
}

static size_t pm_smbios_type19_bytes(const struct pm_smbios_hdr *hdr)
{
	const uint8_t *raw = (const uint8_t *)hdr;

	if (hdr->length < 0x15U) {
		return 0U;
	}

	if (hdr->length >= 0x1FU) {
		const uint64_t start = (uint64_t)raw[0x0F] | ((uint64_t)raw[0x10] << 8) |
				       ((uint64_t)raw[0x11] << 16) | ((uint64_t)raw[0x12] << 24) |
				       ((uint64_t)raw[0x13] << 32) | ((uint64_t)raw[0x14] << 40) |
				       ((uint64_t)raw[0x15] << 48) | ((uint64_t)raw[0x16] << 56);
		const uint64_t end = (uint64_t)raw[0x17] | ((uint64_t)raw[0x18] << 8) |
				     ((uint64_t)raw[0x19] << 16) | ((uint64_t)raw[0x1A] << 24) |
				     ((uint64_t)raw[0x1B] << 32) | ((uint64_t)raw[0x1C] << 40) |
				     ((uint64_t)raw[0x1D] << 48) | ((uint64_t)raw[0x1E] << 56);

		if (end > start) {
			return (size_t)(end - start + 1ULL);
		}

		return 0U;
	}

	const uint32_t start = (uint32_t)raw[0x04] | ((uint32_t)raw[0x05] << 8) |
			       ((uint32_t)raw[0x06] << 16) | ((uint32_t)raw[0x07] << 24);
	const uint32_t end = (uint32_t)raw[0x08] | ((uint32_t)raw[0x09] << 8) |
			     ((uint32_t)raw[0x0A] << 16) | ((uint32_t)raw[0x0B] << 24);

	if (end > start) {
		return (size_t)((uint64_t)end - (uint64_t)start + 1ULL);
	}

	return 0U;
}

static size_t pm_smbios_sum_table(const uint8_t *table, size_t table_len)
{
	size_t total17 = 0U;
	size_t total19 = 0U;
	size_t max16 = 0U;
	size_t offset = 0U;

	while (offset + sizeof(struct pm_smbios_hdr) <= table_len) {
		const struct pm_smbios_hdr *hdr =
			(const struct pm_smbios_hdr *)(table + offset);

		if (hdr->length < sizeof(struct pm_smbios_hdr)) {
			break;
		}
		if (offset + hdr->length > table_len) {
			break;
		}

		switch (hdr->type) {
		case PM_SMBIOS_TYPE_MEMORY_DEVICE:
			total17 += pm_smbios_type17_bytes(hdr);
			break;
		case PM_SMBIOS_TYPE_MEMORY_MAPPED_ADDR: {
			const size_t bytes = pm_smbios_type19_bytes(hdr);

			if (bytes > total19) {
				total19 = bytes;
			}
			break;
		}
		case PM_SMBIOS_TYPE_PHYSICAL_MEMORY_ARRAY: {
			const size_t bytes = pm_smbios_type16_bytes(hdr);

			if (bytes > max16) {
				max16 = bytes;
			}
			break;
		}
		default:
			break;
		}

		offset += hdr->length;
		while (offset + 1U < table_len &&
		       (table[offset] != 0U || table[offset + 1U] != 0U)) {
			offset++;
		}
		offset += 2U;
	}

	size_t best = 0U;

	if (max16 > best) {
		best = max16;
	}
	if (total19 > best) {
		best = total19;
	}
	if (total17 > best) {
		best = total17;
	}

	return best;
}

static size_t pm_smbios_probe_table_copy(uint64_t table_addr, size_t table_len)
{
	uint8_t *table = k_malloc(table_len);
	size_t total = 0U;

	if (table != NULL &&
	    pm_phys_copy(table, (uintptr_t)table_addr, table_len) == 0) {
		total = pm_smbios_sum_table(table, table_len);
	}

	k_free(table);
	return total;
}

static size_t pm_smbios_probe_vendor(void *vendor)
{
	struct pm_smbios3_eps eps3;
	struct pm_smbios_eps eps;

	if (vendor == NULL) {
		return 0U;
	}

	pm_efi_copy_vendor_view(vendor, &eps3, sizeof(eps3));
	if (memcmp(eps3.anchor, "_SM3_", 5) == 0 &&
	    eps3.length >= sizeof(eps3) &&
	    pm_smbios_checksum(&eps3, eps3.length) == 0U &&
	    eps3.table_address != 0ULL && eps3.table_max_size != 0U) {
		return pm_smbios_probe_table_copy(eps3.table_address, eps3.table_max_size);
	}

	pm_efi_copy_vendor_view(vendor, &eps, sizeof(eps));
	if (memcmp(eps.anchor, "_SM_", 4) == 0 &&
	    eps.length >= sizeof(eps) &&
	    pm_smbios_checksum(&eps, eps.length) == 0U &&
	    memcmp(eps.int_anchor, "_DMI_", 5) == 0 &&
	    pm_smbios_checksum(eps.int_anchor, 16U) == 0U &&
	    eps.table_address != 0U && eps.table_length != 0U) {
		return pm_smbios_probe_table_copy(eps.table_address, eps.table_length);
	}

	return pm_smbios_probe_table_copy((uint64_t)(uintptr_t)vendor, 65536U);
}

static size_t pm_efi_smbios_probe_once(void)
{
	void *vendor;
	size_t total = 0U;

	vendor = pm_efi_lookup_vendor_table(&pm_efi_smbios3_guid);
	if (vendor != NULL) {
		total = pm_smbios_probe_vendor(vendor);
	}

	if (total == 0U) {
		vendor = pm_efi_lookup_vendor_table(&pm_efi_smbios_guid);
		if (vendor != NULL) {
			total = pm_smbios_probe_vendor(vendor);
		}
	}

	return total;
}

static pm_port_efi_ram_source_t pm_efi_ram_source;

static void pm_port_efi_ram_source_set(pm_port_efi_ram_source_t source)
{
	pm_efi_ram_source = source;
}

size_t pm_port_efi_machine_ram(void)
{
	static size_t cached;
	static bool probed;

	if (!probed) {
		cached = pm_efi_memmap_probe_once();
		if (cached > 0U) {
			pm_port_efi_ram_source_set(PM_PORT_EFI_RAM_SOURCE_MEMMAP);
		} else {
			cached = pm_efi_smbios_probe_once();
			if (cached > 0U) {
				pm_port_efi_ram_source_set(PM_PORT_EFI_RAM_SOURCE_SMBIOS);
			}
		}
		probed = true;
	}

	return cached;
}

pm_port_efi_ram_source_t pm_port_efi_ram_source(void)
{
	return pm_efi_ram_source;
}

#else /* CONFIG_X86_EFI */

size_t pm_port_efi_machine_ram(void)
{
	return 0U;
}

pm_port_efi_ram_source_t pm_port_efi_ram_source(void)
{
	return PM_PORT_EFI_RAM_SOURCE_NONE;
}

#endif /* CONFIG_X86_EFI */
