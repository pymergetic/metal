/*
 * T2 — print one WASI env var by name (argv[1]), or "(unset)" if it was
 * never exported. wasm32-wasip1. Exercises the runtime's WASI env plumbing
 * (see runtime.h's run_ex() envc/envp, and the shell's `export` builtin).
 */
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	if (argc < 2) {
		printf("t2_env: usage: t2_env <NAME>\n");
		return 1;
	}

	const char *val = getenv(argv[1]);

	printf("t2_env: %s=%s\n", argv[1], val ? val : "(unset)");
	return 0;
}
