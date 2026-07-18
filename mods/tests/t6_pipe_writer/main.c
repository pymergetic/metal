/*
 * T6 — writes a fixed message to its own stdout, then exits. Paired
 * with mods/t7_pipe_reader to exercise runtime/process.h's
 * ownership-transfer contract for stdout_fd/stdin_fd (see process.h's
 * own spawn() doc comment, port/pipe.h): the driver (src/linux/
 * process_test.c) hands this mod's stdout_fd the write end of a real
 * host pipe whose read end t7_pipe_reader gets as its own stdin_fd.
 */
#include <stdio.h>

int main(void)
{
	printf("hello through the pipe\n");
	fflush(stdout);
	return 0;
}
