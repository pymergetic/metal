#ifndef METAL_LWIP_ARCH_CC_H_
#define METAL_LWIP_ARCH_CC_H_

#include <stdint.h>
#include <stddef.h>

/* Let lwip/arch.h typedef u8_t/… from stdint (avoid C11 redefinition warnings). */

#ifndef BYTE_ORDER
#define BYTE_ORDER LITTLE_ENDIAN
#endif

#define LWIP_NO_INTTYPES_H 1
#define LWIP_NO_CTYPE_H 1
#define X8_F "02x"
#define U16_F "u"
#define S16_F "d"
#define X16_F "x"
#define U32_F "u"
#define S32_F "d"
#define X32_F "x"
#define SZT_F "lu"

#define LWIP_PLATFORM_DIAG(x) \
    do {                      \
    } while (0)
#define LWIP_PLATFORM_ASSERT(x) \
    do {                        \
    } while (0)

#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x

#endif /* METAL_LWIP_ARCH_CC_H_ */
