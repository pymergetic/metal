# Host-side boot tree (Metal-style +-- / `--) for scripts/run.
# ASCII only — same glyphs as the guest banner tree.
# shellcheck shell=bash

pm_metal_run_tree() {
	printf '%s\n' "$@" >&2
}

pm_metal_run_tree_begin() {
	local title="${1:-pymergetic metal run}"
	pm_metal_run_tree "+-- ${title}" "|"
}

# Last top-level host-tree node, then a blank line before raw QEMU/OVMF/serial.
pm_metal_run_tree_handover() {
	pm_metal_run_tree "\`-- handover  qemu / ovmf / guest serial"
	pm_metal_run_tree ""
}
