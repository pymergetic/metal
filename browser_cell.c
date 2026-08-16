/* Host stand-in for the emcc metal cell: malloc CLASS_MEM + constructors + probe. */
#include "extmod/metal/boot.h"

#include <stdio.h>

int main(void) {
    if (pm_metal_boot() != 0 || !pm_metal_ready()) {
        fprintf(stderr, "browser metal cell boot\n");
        return 1;
    }
    puts("browser metal cell ok");
    return 0;
}
