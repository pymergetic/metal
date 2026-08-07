/* Freestanding unistd stubs for metalpython embed. */
#ifndef PM_METAL_LIBC_UNISTD_H_
#define PM_METAL_LIBC_UNISTD_H_

#include <stddef.h>
#include <stdint.h>

typedef intptr_t ssize_t;

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

#endif /* PM_METAL_LIBC_UNISTD_H_ */
