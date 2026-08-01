/* tests.wasm_hello_c — minimal C wasm pack (`ready` -> 0). */
#include <stdint.h>

int32_t ready(void)
{
  return 0;
}
