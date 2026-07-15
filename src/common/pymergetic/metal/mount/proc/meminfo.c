#include "pymergetic/metal/mount/proc/meminfo.h"

#include <stdint.h>
#include <stdio.h>

#include "pymergetic/metal/memory/bytecode.h"
#include "pymergetic/metal/memory/kheap.h"
#include "pymergetic/metal/mount/proc/util.h"

int pm_metal_mount_proc_generate_meminfo(char *out, size_t cap, size_t *out_len)
{
	uint64_t kheap = pm_metal_memory_kheap_ops()->bytes();
	uint64_t bytecode = pm_metal_memory_bytecode_ops()->bytes();
	char buf[256];
	int n;

	/* Linux-shaped kB lines, Metal pool totals (not host RAM). */
	n = snprintf(buf, sizeof(buf),
		     "MemTotal:       %8llu kB\n"
		     "BytecodeTotal:  %8llu kB\n",
		     (unsigned long long)(kheap / 1024ull), (unsigned long long)(bytecode / 1024ull));
	if (n < 0 || (size_t)n >= sizeof(buf)) {
		return -1;
	}
	return pm_metal_mount_proc_put_str(out, cap, out_len, buf);
}
