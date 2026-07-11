#include <pymergetic/metal/sys/sys.h>

#include <stdio.h>

int main(void)
{
	if (pm_metal_sys_init() != 0) {
		fprintf(stderr, "mod-smoke: pm_metal_sys_init failed\n");
		return 1;
	}

	printf("mod-smoke: machine_ram=%llu\n", (unsigned long long)pm_metal_sys_machine_ram());
	return 0;
}
