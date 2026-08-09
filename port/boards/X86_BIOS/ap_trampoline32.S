/* i686 bring-up stub: empty AP tramp (SMP long-mode tramp not used). */
	.section .rodata
	.align 4
	.globl pm_metal_ap_tramp
	.globl pm_metal_ap_tramp_size
pm_metal_ap_tramp:
pm_metal_ap_tramp_end:
pm_metal_ap_tramp_size:
	.quad pm_metal_ap_tramp_end - pm_metal_ap_tramp

	.section .note.GNU-stack,"",@progbits
