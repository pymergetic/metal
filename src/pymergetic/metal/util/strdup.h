#ifndef PM_METAL_UTIL_STRDUP_H
#define PM_METAL_UTIL_STRDUP_H

#ifdef __cplusplus
extern "C" {
#endif

char *pm_metal_util_strdup(const char *s);

#ifdef __cplusplus
}

namespace pm::kernel::util {

struct Strdup {
    static char *dup(const char *s) { return pm_metal_util_strdup(s); }
};

inline constexpr Strdup strdup{};

} /* namespace pm::kernel::util */
#endif

#endif /* PM_METAL_UTIL_STRDUP_H */
