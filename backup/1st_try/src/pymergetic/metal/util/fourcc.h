#ifndef PM_METAL_UTIL_FOURCC_H
#define PM_METAL_UTIL_FOURCC_H
#include <stdint.h>

#define PM_METAL_UTIL_FOURCC_LEN 4

/* Big-endian FourCC wire encoding; matches pm_metal_util_fourcc_from_string(). */
#define PM_METAL_UTIL_FOURCC(a, b, c, d)                                                              \
    (((uint32_t)(uint8_t)(a) << 24) | ((uint32_t)(uint8_t)(b) << 16) | ((uint32_t)(uint8_t)(c) << 8) | \
     (uint32_t)(uint8_t)(d))

typedef struct pm_metal_util_fourcc_Tag {
    uint32_t v;
} pm_metal_util_fourcc_Tag;

#ifdef __cplusplus
extern "C" {
#endif

void pm_metal_util_fourcc_from_u32(pm_metal_util_fourcc_Tag *out, uint32_t v);
uint32_t pm_metal_util_fourcc_to_u32(const pm_metal_util_fourcc_Tag *tag);
void pm_metal_util_fourcc_from_bytes(const uint8_t bytes[PM_METAL_UTIL_FOURCC_LEN],
                                      pm_metal_util_fourcc_Tag *out);
void pm_metal_util_fourcc_to_bytes(const pm_metal_util_fourcc_Tag *tag,
                                    uint8_t out[PM_METAL_UTIL_FOURCC_LEN]);
int pm_metal_util_fourcc_from_string(const char *s, pm_metal_util_fourcc_Tag *out);
int pm_metal_util_fourcc_to_string(const pm_metal_util_fourcc_Tag *tag,
                                    char out[PM_METAL_UTIL_FOURCC_LEN + 1]);
int pm_metal_util_fourcc_label(uint32_t magic, char out[PM_METAL_UTIL_FOURCC_LEN + 1]);

#ifdef __cplusplus
}

namespace pm::kernel::util {

struct Fourcc {
    static void from(pm_metal_util_fourcc_Tag *out, uint32_t v) { pm_metal_util_fourcc_from_u32(out, v); }
    static uint32_t to(const pm_metal_util_fourcc_Tag *tag) { return pm_metal_util_fourcc_to_u32(tag); }
    static void load(const uint8_t bytes[PM_METAL_UTIL_FOURCC_LEN], pm_metal_util_fourcc_Tag *out) {
        pm_metal_util_fourcc_from_bytes(bytes, out);
    }
    static void store(const pm_metal_util_fourcc_Tag *tag, uint8_t out[PM_METAL_UTIL_FOURCC_LEN]) {
        pm_metal_util_fourcc_to_bytes(tag, out);
    }
    static int parse(const char *s, pm_metal_util_fourcc_Tag *out) {
        return pm_metal_util_fourcc_from_string(s, out);
    }
    static int format(const pm_metal_util_fourcc_Tag *tag, char out[PM_METAL_UTIL_FOURCC_LEN + 1]) {
        return pm_metal_util_fourcc_to_string(tag, out);
    }
    static int label(uint32_t magic, char out[PM_METAL_UTIL_FOURCC_LEN + 1]) {
        return pm_metal_util_fourcc_label(magic, out);
    }
};

inline constexpr Fourcc fourcc{};

} /* namespace pm::kernel::util */
#endif

#endif /* PM_METAL_UTIL_FOURCC_H */
