/* The knob faces, from C++.
 *
 * A seat's code is written in four languages: C and Rust for card muscle,
 * Python for the seat that drives it, and C++ where the transpile chain and the
 * mrustc shim live. Capacity has to be reachable from all four with the same
 * face, so this translation unit is compiled by the C++ compiler, includes the
 * card's umbrella the way any consumer would, and asks a card for its knobs.
 *
 * It is a consumer, not a second implementation: the muscle is still
 * pymergetic.util.limits' own C. The host runner calls this once.
 */
#include "pymergetic/util/limits.h"

#include <cstdio>
#include <cstring>
#include <string>

static int fail(const char *why) {
    std::fprintf(stderr, "cpp limits prove: %s\n", why);
    return 1;
}

extern "C" int pm_metal_host_cpp_limits_prove(void) {
    const char *mod = "pymergetic.metal.net.ip";
    std::string name;
    int32_t sock;
    uint32_t own;
    uint32_t i;
    uint32_t was;

    if (pm_util_limits_ready() == 0u || pm_util_limits_count() == 0u) {
        return fail("no knobs on this seat");
    }
    own = pm_util_limits_count_of(mod);
    if (own == 0u) {
        return fail("the ip card has no knobs of its own");
    }
    /* The nth knob of a module, and what it says about itself. */
    for (i = 0; i < own; i++) {
        int32_t k = pm_util_limits_nth_of(mod, (int32_t)i);
        if (k < 0) {
            return fail("a module answered short of its own count");
        }
        if (std::strcmp(pm_util_limits_module(k), mod) != 0) {
            return fail("a knob does not name the module it is under");
        }
        name = std::string(mod) + "." + pm_util_limits_leaf(k);
        if (name != pm_util_limits_name(k)) {
            return fail("the name is not the module and the leaf joined");
        }
        if (pm_util_limits_find_of(mod, pm_util_limits_leaf(k)) != k) {
            return fail("a card cannot find its own knob by leaf");
        }
    }
    if (pm_util_limits_nth_of(mod, (int32_t)own) != -1) {
        return fail("a module answered past its own knobs");
    }
    /* A branch reads as a branch. */
    if (pm_util_limits_count_of("pymergetic.metal.drivers") != 0u
        || pm_util_limits_count_under("pymergetic.metal.drivers") == 0u) {
        return fail("the driver branch does not read as a branch");
    }
    /* And a seat's C++ can move one and put it back. */
    sock = pm_util_limits_find_of(mod, "socket");
    if (sock < 0) {
        return fail("no socket knob");
    }
    was = pm_util_limits_soft(sock);
    if (pm_util_limits_set_at(sock, was + 8u) != 0
        || pm_util_limits_soft(sock) != was + 8u) {
        return fail("set by index");
    }
    if (pm_util_limits_reset_at(sock) != 0 || pm_util_limits_soft(sock) != was) {
        return fail("reset by index");
    }
    std::printf("cpp knobs under their module\n");
    return 0;
}
