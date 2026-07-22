/** @file
  Shell `test` — lives with boot bring-up suite. (impl: efi|bios)
**/
#include <pymergetic/metal/shell/shell_cmd.h>
#include <pymergetic/metal/shell/shell/shell.h>
#include <pymergetic/metal/boot/boot.h>
#include <pymergetic/metal/runtime/async/async.h>
#include <runtime/time/time.h>

#include <Uefi.h>

STATIC
VOID
BootTestShellCmd (
  INT32   argc,
  CHAR8 **argv
  )
{
  pm_metal_async_handle_t  test_h;
  pm_metal_async_handle_t  task_h;
  UINT64                   deadline;

  (VOID)argc;
  (VOID)argv;

  if (pm_metal_shell_job_busy ()) {
    pm_metal_shell_out ("test: busy");
    return;
  }

  test_h = pm_metal_boot_tests_start ();
  if (test_h == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_shell_out ("test: start failed");
    return;
  }

  task_h = pm_metal_async_create_task (test_h);
  if (task_h == PM_METAL_ASYNC_HANDLE_INVALID) {
    pm_metal_async_coro_close (test_h);
    pm_metal_shell_out ("test: task failed");
    return;
  }

  /* Suite wall budget: DHCP 10s + proofs (worst ~30s http) + slack. */
  deadline = pm_metal_time_mono_us () + 120000000ull;
  if (pm_metal_shell_job_start ("test", task_h, test_h, NULL, deadline) != 0) {
    pm_metal_async_task_cancel (task_h);
    pm_metal_shell_out ("test: job failed");
    return;
  }

  pm_metal_shell_out ("test: …");
}

PM_METAL_SHELL_CMD (
  g_pm_metal_shell_cmd_test,
  "test",
  "test              run bring-up proof suite",
  BootTestShellCmd
  );
