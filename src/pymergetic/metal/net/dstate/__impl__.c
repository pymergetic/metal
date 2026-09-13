/* pymergetic.metal.net.dstate — distributed state synchronisation.
 *
 * Stores key-value entries in a static array (PM_UTIL_LIMIT_C, max 64).
 * Every entry has a version (Lamport clock) and peer_id so the receiver can
 * decide last-writer-wins vs merge. A set() increments the local Lamport
 * clock and writes the entry; updates are published via zenoh put on
 * "dstate/<key>". A recv() from a zenoh subscriber applies LWW or calls a
 * registered prefix merge handler.
 *
 * Merge handlers are registered by key prefix and stored in a simple array
 * (one per entry, small scale). On a conflict (version tie) the higher
 * peer_id wins as the tie-breaker to give a deterministic outcome.
 *
 * All storage is static, no arena dependency. The peer_id is set at init.
 */
#include "pymergetic/metal/net/dstate/__exports__.h"

#include "pymergetic/util/limits.h"

#include <stdint.h>
#include <string.h>
#include <stdio.h>

#ifndef PM_METAL_DSTATE_ENTRIES_MAX
#define PM_METAL_DSTATE_ENTRIES_MAX 64u
#endif

#ifndef PM_METAL_DSTATE_MERGES_MAX
#define PM_METAL_DSTATE_MERGES_MAX 8u
#endif

/* One merge handler registration. */
struct dstate_merge {
    char prefix[PM_METAL_DSTATE_KEY_MAX];
    pm_metal_dstate_merge_fn fn;
    void *user;
    uint8_t used;
};

static pm_metal_dstate_entry_t s_entries[PM_METAL_DSTATE_ENTRIES_MAX];
static uint32_t s_n; /* live entry count */

static struct dstate_merge s_merges[PM_METAL_DSTATE_MERGES_MAX];
static uint32_t s_nm; /* live merge count */

static uint64_t s_clock; /* local Lamport clock */
static uint32_t s_peer_id;

PM_UTIL_LIMIT_C(pm_dstate_entries_limit, pymergetic.metal.net.dstate, entries,
    PM_METAL_DSTATE_ENTRIES_MAX, 0u, &s_n);
PM_UTIL_LIMIT_C(pm_dstate_merges_limit, pymergetic.metal.net.dstate, merges,
    PM_METAL_DSTATE_MERGES_MAX, 0u, &s_nm);

/* Find an entry by key, returning its index or -1. */
static int32_t find_entry(const char *key) {
    uint32_t i;
    if (key == NULL) {
        return -1;
    }
    for (i = 0; i < s_n; i++) {
        if (strcmp(s_entries[i].key, key) == 0) {
            return (int32_t)i;
        }
    }
    return -1;
}

/* Find a merge handler whose prefix matches the given key, returning the
 * longest-prefix match index or -1. */
static int32_t find_merge(const char *key) {
    uint32_t i;
    int32_t best = -1;
    size_t best_len = 0;
    size_t klen;
    if (key == NULL) {
        return -1;
    }
    klen = strlen(key);
    for (i = 0; i < s_nm; i++) {
        size_t plen;
        if (!s_merges[i].used) {
            continue;
        }
        plen = strlen(s_merges[i].prefix);
        if (plen > klen) {
            continue;
        }
        if (strncmp(key, s_merges[i].prefix, plen) == 0) {
            if ((int32_t)plen > (int32_t)best_len) {
                best = (int32_t)i;
                best_len = plen;
            }
        }
    }
    return best;
}

int32_t pm_metal_dstate_init(uint32_t peer_id) {
    s_peer_id = peer_id;
    s_clock = 0;
    s_n = 0;
    s_nm = 0;
    memset(s_entries, 0, sizeof(s_entries));
    memset(s_merges, 0, sizeof(s_merges));
    return 0;
}

/* Bump the local Lamport clock past the given value. */
static void bump_clock(uint64_t incoming) {
    if (incoming >= s_clock) {
        s_clock = incoming + 1;
    } else {
        s_clock++;
    }
}

int32_t pm_metal_dstate_set(const char *key, const uint8_t *value, uint32_t value_len) {
    int32_t idx;
    if (key == NULL || key[0] == '\0' || (value_len && value == NULL)) {
        return -1;
    }
    if (value_len > PM_METAL_DSTATE_VALUE_MAX) {
        return -1;
    }
    bump_clock(0);
    idx = find_entry(key);
    if (idx >= 0) {
        /* Update existing entry. */
        if (value_len > 0) {
            memcpy(s_entries[idx].value, value, value_len);
        }
        s_entries[idx].value_len = value_len;
        s_entries[idx].version = s_clock;
        s_entries[idx].peer_id = s_peer_id;
        return 0;
    }
    /* New entry. */
    if (s_n >= PM_METAL_DSTATE_ENTRIES_MAX) {
        return -1;
    }
    memset(&s_entries[s_n], 0, sizeof(s_entries[s_n]));
    strncpy(s_entries[s_n].key, key, sizeof(s_entries[s_n].key) - 1);
    s_entries[s_n].key[sizeof(s_entries[s_n].key) - 1] = '\0';
    if (value_len > 0) {
        memcpy(s_entries[s_n].value, value, value_len);
    }
    s_entries[s_n].value_len = value_len;
    s_entries[s_n].version = s_clock;
    s_entries[s_n].peer_id = s_peer_id;
    s_n++;
    return 0;
}

int32_t pm_metal_dstate_get(const char *key, uint8_t *value, uint32_t *value_len) {
    int32_t idx;
    if (key == NULL || value == NULL || value_len == NULL) {
        return -1;
    }
    idx = find_entry(key);
    if (idx < 0) {
        return -1; /* not found */
    }
    if (s_entries[idx].value_len > PM_METAL_DSTATE_VALUE_MAX) {
        return -1;
    }
    if (s_entries[idx].value_len > 0) {
        if (*value_len < s_entries[idx].value_len) {
            return -2; /* buffer too small */
        }
        memcpy(value, s_entries[idx].value, s_entries[idx].value_len);
    }
    *value_len = s_entries[idx].value_len;
    return 0;
}

int32_t pm_metal_dstate_del(const char *key) {
    int32_t idx;
    if (key == NULL || key[0] == '\0') {
        return -1;
    }
    idx = find_entry(key);
    if (idx < 0) {
        return -1; /* not found */
    }
    /* Compact: swap with last. */
    s_n--;
    if (idx < (int32_t)s_n) {
        s_entries[idx] = s_entries[s_n];
    }
    memset(&s_entries[s_n], 0, sizeof(s_entries[s_n]));
    return 0;
}

int32_t pm_metal_dstate_merge_register(const char *key_prefix, pm_metal_dstate_merge_fn fn, void *user) {
    if (key_prefix == NULL || key_prefix[0] == '\0' || fn == NULL) {
        return -1;
    }
    if (s_nm >= PM_METAL_DSTATE_MERGES_MAX) {
        return -1;
    }
    memset(&s_merges[s_nm], 0, sizeof(s_merges[s_nm]));
    strncpy(s_merges[s_nm].prefix, key_prefix, sizeof(s_merges[s_nm].prefix) - 1);
    s_merges[s_nm].prefix[sizeof(s_merges[s_nm].prefix) - 1] = '\0';
    s_merges[s_nm].fn = fn;
    s_merges[s_nm].user = user;
    s_merges[s_nm].used = 1;
    s_nm++;
    return 0;
}

int32_t pm_metal_dstate_recv(const char *key, const uint8_t *value, uint32_t value_len,
    uint64_t version, uint32_t from_peer) {
    int32_t idx;
    int32_t merge_idx;
    if (key == NULL || (value_len && value == NULL)) {
        return -1;
    }
    if (from_peer == s_peer_id) {
        return 0; /* ignore own echo */
    }
    idx = find_entry(key);
    if (idx >= 0) {
        uint64_t local_ver = s_entries[idx].version;
        /* If incoming version is strictly newer, overwrite. */
        if (version > local_ver) {
            if (value_len <= PM_METAL_DSTATE_VALUE_MAX) {
                if (value_len > 0) {
                    memcpy(s_entries[idx].value, value, value_len);
                }
                s_entries[idx].value_len = value_len;
            }
            s_entries[idx].version = version;
            s_entries[idx].peer_id = from_peer;
            bump_clock(version);
            return 0;
        }
        /* Same version: try merge handler first, then tiebreaker. */
        if (version == local_ver) {
            merge_idx = find_merge(key);
            if (merge_idx >= 0 && s_merges[merge_idx].fn != NULL) {
                uint8_t merged[PM_METAL_DSTATE_VALUE_MAX];
                uint32_t merged_len = sizeof(merged);
                int32_t mr;
                memset(merged, 0, merged_len);
                mr = s_merges[merge_idx].fn(
                    s_entries[idx].value, s_entries[idx].value_len,
                    value, value_len,
                    merged, &merged_len,
                    s_merges[merge_idx].user);
                if (mr == 0) {
                    if (merged_len <= PM_METAL_DSTATE_VALUE_MAX) {
                        if (merged_len > 0) {
                            memcpy(s_entries[idx].value, merged, merged_len);
                        }
                        s_entries[idx].value_len = merged_len;
                    }
                    bump_clock(version);
                    s_entries[idx].version = s_clock;
                    s_entries[idx].peer_id = s_peer_id;
                    return 0; /* merged */
                }
                /* Merge failed: keep local, do not fall through. */
                return 0;
            }
            /* No merge handler: tie-break by higher peer_id. */
            if (from_peer > s_entries[idx].peer_id) {
                if (value_len <= PM_METAL_DSTATE_VALUE_MAX) {
                    if (value_len > 0) {
                        memcpy(s_entries[idx].value, value, value_len);
                    }
                    s_entries[idx].value_len = value_len;
                }
                s_entries[idx].peer_id = from_peer;
            }
            bump_clock(version);
            return 0;
        }
        /* Incoming is older or same-version lose: keep local. */
        return 0;
    }
    /* New key from a peer: insert. */
    if (s_n >= PM_METAL_DSTATE_ENTRIES_MAX) {
        return -1;
    }
    memset(&s_entries[s_n], 0, sizeof(s_entries[s_n]));
    strncpy(s_entries[s_n].key, key, sizeof(s_entries[s_n].key) - 1);
    s_entries[s_n].key[sizeof(s_entries[s_n].key) - 1] = '\0';
    if (value_len <= PM_METAL_DSTATE_VALUE_MAX && value_len > 0) {
        memcpy(s_entries[s_n].value, value, value_len);
    }
    s_entries[s_n].value_len = value_len <= PM_METAL_DSTATE_VALUE_MAX ? value_len : 0;
    s_entries[s_n].version = version;
    s_entries[s_n].peer_id = from_peer;
    bump_clock(version);
    s_n++;
    return 0;
}

uint32_t pm_metal_dstate_count(void) {
    return s_n;
}

int32_t pm_metal_dstate_at(uint32_t idx, pm_metal_dstate_entry_t *out) {
    if (out == NULL || idx >= s_n) {
        return -1;
    }
    *out = s_entries[idx];
    return 0;
}

int32_t pm_metal_dstate_publish(void) {
    /* Walk all local entries and publish each via zenoh put on "dstate/<key>".
     * The actual zenoh put call (pm_metal_net_zenoh_put) is wired when a zenoh
     * session is available. For now the data structure is armable. */
    return (int32_t)s_n;
}

#include "pymergetic/wasmmod/guest.h"

PM_MOD_EXPORT_C(pymergetic.metal.net.dstate, pm_metal_dstate_init, pm_metal_dstate_init, int32_t(uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.dstate, pm_metal_dstate_set, pm_metal_dstate_set, int32_t(const char *, const uint8_t *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.dstate, pm_metal_dstate_get, pm_metal_dstate_get, int32_t(const char *, uint8_t *, uint32_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.dstate, pm_metal_dstate_del, pm_metal_dstate_del, int32_t(const char *));
PM_MOD_EXPORT_C(pymergetic.metal.net.dstate, pm_metal_dstate_merge_register, pm_metal_dstate_merge_register, int32_t(const char *, pm_metal_dstate_merge_fn, void *));
PM_MOD_EXPORT_C(pymergetic.metal.net.dstate, pm_metal_dstate_recv, pm_metal_dstate_recv, int32_t(const char *, const uint8_t *, uint32_t, uint64_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.dstate, pm_metal_dstate_count, pm_metal_dstate_count, uint32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.dstate, pm_metal_dstate_at, pm_metal_dstate_at, int32_t(uint32_t, pm_metal_dstate_entry_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.dstate, pm_metal_dstate_publish, pm_metal_dstate_publish, int32_t(void));