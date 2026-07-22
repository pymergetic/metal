/*
 * Port-neutral firmware ownership + takeover (host-only).
 * Product code uses the wrappers; never call bios or efi entrypoints.
 *
 * impl: bios - src/bios/pymergetic/metal/boot/run_port.c
 * impl: efi  - src/efi/pymergetic/metal/boot/run_port.c
 */
#ifndef PYMERGETIC_METAL_BOOT_PORT_H_
#define PYMERGETIC_METAL_BOOT_PORT_H_

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

typedef struct pm_metal_port_ops {
	int (*owned)(void);
	/**
	 * Enter owned floor (seed init + runloop). May not return.
	 * image_handle: EFI ImageHandle, or NULL on BIOS.
	 * Returns 0 on success paths that return; negative on failure.
	 */
	int (*takeover_and_run)(void *image_handle, unsigned n_cpus);
	/** reboot: 0 power-off, non-zero restart. Does not return. */
	void (*reset)(int reboot);
	void (*set_framebuffer)(void *fb, unsigned width, unsigned height,
				unsigned ppsl);
	int (*get_framebuffer)(void **fb, unsigned *width, unsigned *height,
				unsigned *ppsl);
} pm_metal_port_ops_t;

/** Single linked port provides this table. */
extern const pm_metal_port_ops_t pm_metal_port_ops;

static inline int
pm_metal_port_owned(void)
{
	return pm_metal_port_ops.owned();
}

static inline int
pm_metal_port_takeover_and_run(void *image_handle, unsigned n_cpus)
{
	return pm_metal_port_ops.takeover_and_run(image_handle, n_cpus);
}

static inline void
pm_metal_port_reset(int reboot)
{
	pm_metal_port_ops.reset(reboot);
}

static inline void
pm_metal_port_set_framebuffer(void *fb, unsigned width, unsigned height,
			      unsigned ppsl)
{
	pm_metal_port_ops.set_framebuffer(fb, width, height, ppsl);
}

static inline int
pm_metal_port_get_framebuffer(void **fb, unsigned *width, unsigned *height,
			      unsigned *ppsl)
{
	return pm_metal_port_ops.get_framebuffer(fb, width, height, ppsl);
}

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_BOOT_PORT_H_ */
