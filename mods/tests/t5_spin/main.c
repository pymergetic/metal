/*
 * T5 — spins forever unless force-stopped from the outside. Exists to
 * exercise runtime/process.h's kill() end to end: proves WAMR's
 * interpreter loop actually notices wasm_runtime_terminate() mid-flight
 * (see runtime.h's own pm_metal_runtime_terminate() doc comment), not
 * just at an import-call boundary a real workload might never reach.
 * `i` is volatile purely so the loop survives optimization — this mod
 * has no other job than to keep running.
 */
int main(void)
{
	volatile long i = 0;

	for (;;) {
		i++;
	}

	return 0;
}
