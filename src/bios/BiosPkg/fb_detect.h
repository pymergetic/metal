#ifndef PYMERGETIC_METAL_BIOS_FB_DETECT_H_
#define PYMERGETIC_METAL_BIOS_FB_DETECT_H_

/*
 * BIOS framebuffer detectors — same model as blk/net:
 * each always runs; registers an LFB if hardware/firmware provides one.
 * Multiboot FB is registered during Multiboot parse (before these).
 */

/** Bochs/QEMU std VGA (PCI 1234:1111) programmable LFB. */
int pm_bios_fb_bochs_detect(void);

/** VESA BIOS INT 10 LFB modeset (needs real-mode bounce when available). */
int pm_bios_fb_vesa_detect(void);

#endif
