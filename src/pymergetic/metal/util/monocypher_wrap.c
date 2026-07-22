/*
 * Compile vendored Monocypher without editing external/monocypher.
 * EDK2 Base.h defines MIN/MAX before this TU; Monocypher redefines them.
 */
#ifdef MIN
#undef MIN
#endif
#ifdef MAX
#undef MAX
#endif

#include "../../../../external/monocypher/src/monocypher.c"
