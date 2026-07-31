/* Append shell command tables into .data (writable — EFI is PIE).
 * Each slot is a 16-byte-aligned pm_metal_shell_cmd_table_t.
 */
SECTIONS
{
  .data :
  {
    . = ALIGN(16);
    PROVIDE_HIDDEN (__pm_metal_shell_cmds_start = .);
    KEEP (*(SORT_BY_NAME (.pm_metal_shell_cmds.*)));
    PROVIDE_HIDDEN (__pm_metal_shell_cmds_end = .);
    . = ALIGN(16);
    PROVIDE_HIDDEN (__pm_metal_py_binds_start = .);
    KEEP (*(SORT_BY_NAME (.pm_metal_py_binds.*)));
    PROVIDE_HIDDEN (__pm_metal_py_binds_end = .);
    . = ALIGN(16);
    PROVIDE_HIDDEN (__pm_metal_keyb_layouts_start = .);
    KEEP (*(SORT_BY_NAME (.pm_metal_keyb_layouts.*)));
    PROVIDE_HIDDEN (__pm_metal_keyb_layouts_end = .);
    . = ALIGN(16);
    PROVIDE_HIDDEN (__pm_metal_externals_start = .);
    KEEP (*(SORT_BY_NAME (.pm_metal_externals.*)));
    PROVIDE_HIDDEN (__pm_metal_externals_end = .);
    . = ALIGN(16);
    PROVIDE_HIDDEN (__pm_metal_mem_limits_start = .);
    KEEP (*(SORT_BY_NAME (.pm_metal_mem_limits.*)));
    PROVIDE_HIDDEN (__pm_metal_mem_limits_end = .);
  }
}
INSERT AFTER .data;
