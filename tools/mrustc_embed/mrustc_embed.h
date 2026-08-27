/* mrustc in-process embed — C face for the jit.rs card.
 *
 * mrustc is a batch C++ compiler whose `main()` (the CLI driver) is excluded
 * from `bin/mrustc.a`. This shim reproduces that driver's pipeline and runs it
 * in-process: no subprocess, no `system()`, no gcc. The Rust source is written
 * to a temp file (mrustc's lexer is filename-based), compiled down to C, and
 * the generated `.c` is returned to the caller for the TCC/WASM stage.
 *
 * The archive is compiled with -DMRUSTC_EMBED so Span::error()/Span::bug()
 * throw instead of abort()ing the host process.
 */
#ifndef MRUSTC_EMBED_H
#define MRUSTC_EMBED_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Compile Rust source to C, in-process. Returns 0 on success and fills
 * *c_out_len with the generated C length (NUL-terminated into c_out). Returns
 * non-zero on any compile failure. c_out must be at least c_out_cap bytes. */
int pm_metal_jit_rs_mrustc_compile(
    const char *rs_source, size_t rs_len,
    char *c_out, size_t c_out_cap, size_t *c_out_len);

#ifdef __cplusplus
}
#endif

#endif /* MRUSTC_EMBED_H */
