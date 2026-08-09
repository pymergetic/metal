#ifndef PM_METAL_CONSOLE_SMOKE_H_
#define PM_METAL_CONSOLE_SMOKE_H_

#ifdef __cplusplus
extern "C" {
#endif

/* Init ring, attach UART sink, write+replay check. Prints "console ok\n". */
int pm_metal_console_smoke(void);

#ifdef __cplusplus
}
#endif

#endif
