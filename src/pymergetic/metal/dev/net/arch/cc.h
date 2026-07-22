/*
 * lwIP arch/cc.h — Metal EFI freestanding port.
 */
#ifndef METAL_LWIP_ARCH_CC_H_
#define METAL_LWIP_ARCH_CC_H_

#include <stdint.h>
#include <stddef.h>

typedef uint8_t   u8_t;
typedef int8_t    s8_t;
typedef uint16_t  u16_t;
typedef int16_t   s16_t;
typedef uint32_t  u32_t;
typedef int32_t   s32_t;
typedef uintptr_t mem_ptr_t;

#define BYTE_ORDER LITTLE_ENDIAN

/* No hosted inttypes.h under freestanding clangd / EDK2. */
#define LWIP_NO_INTTYPES_H 1
#define LWIP_NO_CTYPE_H 1
#define X8_F   "02x"
#define U16_F  "u"
#define S16_F  "d"
#define X16_F  "x"
#define U32_F  "u"
#define S32_F  "d"
#define X32_F  "x"
#define SZT_F  "lu"

#define LWIP_PLATFORM_DIAG(x) do { } while (0)
#define LWIP_PLATFORM_ASSERT(x) do { } while (0)

#ifndef LWIP_RAND
#define LWIP_RAND() ((u32_t)0xA5A5A5A5u)
#endif

#define PACK_STRUCT_BEGIN
#define PACK_STRUCT_STRUCT __attribute__((packed))
#define PACK_STRUCT_END
#define PACK_STRUCT_FIELD(x) x

#endif /* METAL_LWIP_ARCH_CC_H_ */
