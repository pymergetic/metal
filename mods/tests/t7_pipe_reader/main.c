/*
 * T7 — reads whatever mods/t6_pipe_writer wrote (via a real host pipe
 * the driver wired stdin_fd to — see process.h's spawn() and
 * port/pipe.h) until EOF, then echoes it back prefixed on its own
 * stdout. EOF only actually arrives once the writer's own worker thread
 * closes its end of the pipe on run_ex() return — see process.c's
 * worker — so this mod blocking on fgets() is itself part of what this
 * whole test proves: not just that bytes cross the pipe, but that this
 * side isn't left hanging forever waiting for a close that never comes.
 */
#include <stdio.h>

int main(void)
{
	char line[256];

	if (fgets(line, sizeof(line), stdin)) {
		printf("t7_pipe_reader: got: %s", line);
	} else {
		printf("t7_pipe_reader: got: (nothing)\n");
	}
	fflush(stdout);
	return 0;
}
