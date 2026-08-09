/*
 * Co-located reg seat test for process.
 */
#include <pymergetic/metal/process/__init__.h>

int32_t pm_metal_process_seat_test(void)
{
    pm_metal_process_info_t infos[4];
    (void)pm_metal_process_current();
    (void)pm_metal_process_list(infos, 4);
    (void)pm_metal_process_shutting_down();
    return 0;
}
