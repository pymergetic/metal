# Metal SYMBOLS — C ↔ RS ↔ Py (what links today)

Callee = one language in `src/`. Names below are the C ABI border (`pm_metal_<path>_*`).

## async

| C | Notes |
|---|--------|
| `pm_metal_async_start` | N runners from ACPI count (product: N≥2) |
| `pm_metal_async_run_poll` | After SMP: current CPU only |
| `pm_metal_async_run_poll_cpu` | Explicit runner |
| `pm_metal_async_run_loop_cpu` | AP forever loop |
| `pm_metal_async_create_task` / `_on` | RR or pinned runner |
| `pm_metal_async_sleep_us` / `yield` / `await` | handles |
| `pm_metal_smp_start` | BIOS INIT-SIPI · UEFI EFI MP |
| `pm_metal_smp_cpu_index` / `online_count` | per-CPU |

## dev/acpi

| C | Notes |
|---|--------|
| `pm_metal_dev_acpi_init` | RSDP + MADT |
| `pm_metal_dev_acpi_cpu_count` / `apic_id` / `lapic_base` | SMP |
| `pm_metal_dev_acpi_set_rsdp` / `rsdp` | EFI seed |

## net (packaged)

| C prefix | Callee |
|----------|--------|
| `pm_metal_net_ip_*` | C `net/ip/` |
| `pm_metal_net_dhcp_*` | C `net/dhcp/` |
| `pm_metal_net_dns_*` | C `net/dns/` |
| `pm_metal_net_http_*` | C `net/http/` (mini server/client — not ASGI) |
| `pm_metal_net_ntp_*` | C `net/ntp/` |
| `pm_metal_net_tftp_*` | C `net/tftp/` |
| `pm_metal_net_ssh_*` | C `net/ssh/` (ident+KEX through NEWKEYS; encrypt/auth TODO) |
| `pm_metal_net_pump_*` | C `net/pump/` |
| `pm_metal_net_faces_*` | C `net/faces/` |
| `pm_metal_net_upy_nic_*` | C `net/upy_nic/` |

## Not in product tree (do not document as landed)

- ASGI / Microdot Inspect UI (`asgi/`, `pymergetic.metal.inspect` mod) — planned
- `crates/pm_metal/` full façade — pending
- `typings/pymergetic/metal/` full tree — pending
