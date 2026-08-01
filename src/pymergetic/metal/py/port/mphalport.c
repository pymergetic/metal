/* Thin C HAL — stdout via Metal log when linked; stubs safe for host smoke. */
#include "mphalport.h"

#include <stddef.h>
#include <stdint.h>

/* Declared by Metal when firmware links log; weak so host smoke links. */
void pm_metal_log(const char *line) __attribute__((weak));

void mp_hal_stdout_tx_strn(const char *str, size_t len)
{
	(void)str;
	(void)len;
	/* Full console wiring lands with the VM loop bring-up. */
}

int mp_hal_stdin_rx_chr(void)
{
	return -1;
}

uint64_t mp_hal_ticks_us(void)
{
	return 0;
}

void mp_hal_delay_us(uint32_t us)
{
	(void)us;
}
