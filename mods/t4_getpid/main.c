/*
 * T4 — prints its own pid, read back via plain getenv("PID") — no new
 * host import: runtime/process.h's spawn() injects "PID=<n>" into every
 * process's own env automatically (see process.h's own getpid() note),
 * so any language with ordinary libc env access already has this for
 * free.
 */
#include <stdio.h>
#include <stdlib.h>

int main(void)
{
	const char *pid = getenv("PID");

	printf("t4_getpid: PID=%s\n", pid ? pid : "(unset)");
	return 0;
}
