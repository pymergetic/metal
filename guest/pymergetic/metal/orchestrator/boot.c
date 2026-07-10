#include <pymergetic/metal/orchestrator/boot.h>
#include <pymergetic/metal/sys/sys.h>

#include <stdio.h>

int pm_metal_orchestrator_boot(void)
{
	if (pm_metal_sys_init() != 0) {
		fprintf(stderr, "orchestrator: pm_metal_sys_init failed\n");
		return 1;
	}

	printf("pymergetic orchestrator\n");
	printf("  machine_ram = %llu\n", (unsigned long long)pm_metal_sys_machine_ram());
	printf("  arena_budget = %llu\n", (unsigned long long)pm_metal_sys_arena_budget());
	printf("  ready\n");
	return 0;
}
