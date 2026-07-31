/*
 * Virtio-input tablet (absolute pointer) — survives ExitBootServices.
 * QEMU: -device virtio-tablet-pci
 *
 * impl: common — src/pymergetic/metal/dev/input/virtio_input.c
 */
#ifndef PYMERGETIC_METAL_DEV_INPUT_VIRTIO_INPUT_H_
#define PYMERGETIC_METAL_DEV_INPUT_VIRTIO_INPUT_H_

#ifdef __cplusplus
extern "C" {
#endif

#if !defined(__wasm__)

/** Probe virtio-tablet; 0 on success (absolute axes present). */
int pm_metal_input_virtio_tablet_probe(void);
int pm_metal_input_virtio_tablet_ready(void);
/** Drain eventq → pointer rings / sample. */
void pm_metal_input_virtio_tablet_poll(void);

#endif

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DEV_INPUT_VIRTIO_INPUT_H_ */
