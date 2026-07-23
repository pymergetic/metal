/*
 * METAL-006 — only the executing candidate may authorize boot trust.
 */
#include <stdio.h>
#include <string.h>

static int
verify_loaded(const char *loaded, const char *ok_path, int ok_sig_valid)
{
	if (loaded == NULL || loaded[0] == '\0') {
		return 1; /* missing identity */
	}
	if (strcmp(loaded, ok_path) != 0) {
		return -1; /* must not accept a different candidate */
	}
	return ok_sig_valid ? 0 : -1;
}

int
main(void)
{
	/* Boot BOOTX64.EFI (bad); metal.efi still has a valid sig — reject. */
	if (verify_loaded("EFI/BOOT/BOOTX64.EFI", "metal.efi", 1) != -1) {
		fprintf(stderr, "metal006: accepted non-loaded candidate\n");
		return 1;
	}
	/* Boot the signed artifact — accept. */
	if (verify_loaded("EFI/BOOT/BOOTX64.EFI", "EFI/BOOT/BOOTX64.EFI", 1)
	    != 0) {
		fprintf(stderr, "metal006: rejected loaded candidate\n");
		return 1;
	}
	/* Alter only the executed candidate — fail. */
	if (verify_loaded("EFI/BOOT/BOOTX64.EFI", "EFI/BOOT/BOOTX64.EFI", 0)
	    != -1) {
		fprintf(stderr, "metal006: accepted bad loaded sig\n");
		return 1;
	}
	printf("metal006: ok\n");
	return 0;
}
