/* pymergetic.metal.net.zenoh — Zenoh v1 as a Metal card. One defining lang = C.
 * Owns a small set of wrapped zenoh-pico sessions ("slots", Z_FEATURE_MULTI_THREAD=0
 * "spin" mode). Every step is a bounded, cooperative open or poll; nothing blocks
 * the card, and no pointer is held across a step (user callbacks resolve from the
 * session inside one poll()).
 *
 * Slots let one thread host a listener and a connector on the same _lo_ net.ip:
 * the two-session put/subscriber round-trip prove. sel(0/1) selects which slot the
 * peer()/up()/put()/subscribe() faces drive; poll() (and the platform yield)
 * advance every open slot so the peer that must answer a handshake makes progress
 * while its partner waits.
 *
 * The sessions reach the wire only through pm_metal_net_ip_* (platform_metal.c
 * maps the zenoh-pico system layer). When a slot's read/connect would block, the
 * platform pumps net.ip and spins the other slots' executors
 * (pm_metal_net_zenoh_yield). */
#include "pymergetic/wasmmod/guest.h"

#include "pymergetic/metal/net/zenoh/__priv__.h"

#include "pymergetic/metal/net/ip.h"
#include "pymergetic/metal/async.h"
#include "pymergetic/util/mem.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <zenoh-pico.h>

/* Session whose executor the platform can spin while this session would block.
 * The card supports a small peer set so up()/poll() can drive a connector and a
 * listener cooperatively on one _lo_ net.ip. */
typedef struct pm_metal_net_zenoh_ctx {
    z_owned_session_t session;   /* owned once up() reaches OPEN */
    z_owned_config_t config;     /* built by peer()/up(), kept for reconnect */
    z_owned_liveliness_token_t token;
    uint8_t token_set;
    uint8_t zid[PM_METAL_NET_ZENOH_ZID_LEN];
    uint8_t zid_set;
    uint8_t mode;                /* 0=client,1=peer */
    uint32_t peer_ip;            /* connect (client) or listen bind (peer) addr, be */
    uint16_t peer_port;
    int32_t open_state;          /* 0=idle,1=open-in-progress,2=open,3=failed */
    uint64_t retry_at_us;
    /* Subscriber delivery. subscribe() keeps the z_owned_subscriber_t here (not
     * dropped at declare) so samples delivered by zp_spin_once inside a poll()
     * or yield fire the value callback synchronously, exactly once per sample,
     * inside the step — no pointer is held across a step. */
    z_owned_subscriber_t sub;
    uint8_t sub_set;
    pm_metal_net_zenoh_value_cb_t sub_cb;
    void *sub_arg;
} pm_metal_net_zenoh_ctx_t;

#define PM_METAL_NET_ZENOH_SLOTS 2u

static pm_metal_net_zenoh_ctx_t s_slots[PM_METAL_NET_ZENOH_SLOTS];
static uint8_t s_sel;            /* slot the bounded faces drive */
static pm_util_mem_arena_t *s_arena;
static pm_metal_net_zenoh_yield_fn s_yield_hook;
/* Slot whose zenoh-pico executor is running on the current call stack (inside a
 * z_open / zp_spin_once). The platform yield spins every open slot EXCEPT any
 * whose executor is already on the stack: that interleaves two sessions' blocked
 * handshake reads without re-entering a running executor. */
static uint8_t s_exec_in[PM_METAL_NET_ZENOH_SLOTS];
static uint8_t s_local_zid[PM_METAL_NET_ZENOH_ZID_LEN];
static int32_t s_local_zid_init;

pm_util_mem_arena_t *pm_metal_net_zenoh_arena(void) {
    return s_arena;
}

void pm_metal_net_zenoh_set_yield(pm_metal_net_zenoh_yield_fn fn) {
    s_yield_hook = fn;
}

/* Spin every OPEN slot's executor for a few bounded steps. Used by yield() so a
 * slot that would block on the wire still lets its peers make progress, and by
 * poll() so one pump loop advances the whole card. A handful of spins lets both
 * the sender and the receiver tasks of a session get CPU (zp_spin_once runs one
 * ready task).
 *
 * A slot whose executor already is running on the call stack (s_exec_in set,
 * e.g. we are inside its z_open / zp_spin_once right now) is skipped so that the
 * yield from one session's blocked read spins the matching peer instead of
 * re-entering a running executor (zenoh-pico spin mode is not re-entrant). */
static void spin_open_slots(void) {
    uint8_t i;
    int s;
    for (i = 0; i < PM_METAL_NET_ZENOH_SLOTS; i++) {
        if (s_slots[i].open_state != 2 || s_exec_in[i]) {
            continue;
        }
        s_exec_in[i] = 1;
        /* A CLIENT slot's transport has one base socket that carries its
         * keep-alives; a PEER (listener) slot's transport has NO base data
         * socket — zebala sends to peers via their accepted sockets, and the
         * transport's base link is the LISTEN socket, which can never accept a
         * write. Sending a keep-alive there is a guaranteed -1 that, in the
         * hot yield loop, becomes a multiple-megabyte busy spin and starves the
         * very handshake we are trying to advance. So keep-alive only clients. */
        if (s_slots[i].mode == 0u) {
            (void)zp_send_keep_alive(z_loan(s_slots[i].session), NULL);
        }
        for (s = 0; s < 8; s++) {
            (void)zp_spin_once(z_loan(s_slots[i].session));
        }
        s_exec_in[i] = 0;
    }
}

void pm_metal_net_zenoh_yield(void) {
    pm_metal_net_ip_pump();
    spin_open_slots();
    if (s_yield_hook != NULL) {
        s_yield_hook();
    }
}

static void zenoh_local_zid(uint8_t out[PM_METAL_NET_ZENOH_ZID_LEN]) {
    /* Stable per-process pre-open ZID, cached on first read so two zid() calls in
     * one boot agree (the border prove reads it twice). Derived from net.ip lo
     * (127.0.0.1) + the high bits of the monotonic clock so a fresh boot gets a
     * fresh identity even on a fixed MAC. zenoh-pico will overwrite this with a
     * random one anyway when we do not pin it; we keep the face's copy in sync
     * from z_info_zid() after open. */
    uint64_t t;
    uint32_t i;
    if (s_local_zid_init) {
        memcpy(out, s_local_zid, PM_METAL_NET_ZENOH_ZID_LEN);
        return;
    }
    t = pm_metal_async_mono_us();
    memset(s_local_zid, 0, PM_METAL_NET_ZENOH_ZID_LEN);
    s_local_zid[0] = 0x4d;
    s_local_zid[1] = 0x65;
    s_local_zid[2] = 0x7a;
    s_local_zid[3] = 0x01;
    for (i = 0; i < 8; i++) {
        s_local_zid[4 + i] = (uint8_t)(t >> (8u * i));
    }
    s_local_zid[12] = 0x7f;
    s_local_zid[13] = 0x00;
    s_local_zid[14] = 0x00;
    s_local_zid[15] = 0x01;
    s_local_zid_init = 1;
    memcpy(out, s_local_zid, PM_METAL_NET_ZENOH_ZID_LEN);
}

/* Build the locator string "tcp/127.0.0.1:7447" from the slot's peer() args. */
static void fill_locator(pm_metal_net_zenoh_ctx_t *sl, char *dst, size_t dstlen) {
    (void)snprintf(dst, dstlen, "tcp/%u.%u.%u.%u:%u", (unsigned)(sl->peer_ip >> 24),
        (unsigned)((sl->peer_ip >> 16) & 0xff), (unsigned)((sl->peer_ip >> 8) & 0xff),
        (unsigned)(sl->peer_ip & 0xff), (unsigned)sl->peer_port);
}

/* One bounded, cooperative attempt to bring the slot's session to OPEN. Returns
 * 1 when OPEN, 0 when still in progress (caller should poll() again), -1 on hard
 * failure (bad config, no locator yet). Never blocks the card beyond the
 * platform's bounded connect/read budgets. */
static int32_t try_open(pm_metal_net_zenoh_ctx_t *sl) {
    char locator[40];
    z_owned_session_t *zs = &sl->session;
    z_result_t rc;

    if (sl->open_state == 2) {
        return 1;
    }
    if (sl->open_state == 1 && pm_metal_async_mono_us() < sl->retry_at_us) {
        return 0; /* backoff: not time to retry yet */
    }
    if (sl->peer_ip == 0u) {
        return -1; /* no peer configured via peer() yet */
    }

    z_config_default(&sl->config);
    fill_locator(sl, locator, sizeof(locator));
    if (sl->mode == 0u) {
        zp_config_insert(z_loan_mut(sl->config), Z_CONFIG_MODE_KEY, Z_CONFIG_MODE_CLIENT);
        zp_config_insert(z_loan_mut(sl->config), Z_CONFIG_CONNECT_KEY, locator);
    } else {
        zp_config_insert(z_loan_mut(sl->config), Z_CONFIG_MODE_KEY, Z_CONFIG_MODE_PEER);
        zp_config_insert(z_loan_mut(sl->config), Z_CONFIG_LISTEN_KEY, locator);
    }

    sl->open_state = 1;
    s_exec_in[(size_t)(sl - s_slots)] = 1;
    rc = z_open(zs, z_move(sl->config), NULL);
    s_exec_in[(size_t)(sl - s_slots)] = 0;
    if (rc != Z_OK) {
        /* The platform cooperated for a bounded time and the peer did not
         * answer yet (classic single-thread case where poll() must retry). Put
         * the session back to idle and retry after a backoff. */
        sl->open_state = 0;
        sl->retry_at_us = pm_metal_async_mono_us() + 250000ull; /* 250 ms */
        return 0;
    }

    /* OPEN. Sync our ZID copy from the session. */
    {
        z_id_t zid = z_info_zid(z_loan(*zs));
        memcpy(sl->zid, zid.id, PM_METAL_NET_ZENOH_ZID_LEN);
        sl->zid_set = 1;
    }
        /* Liveliness token at @/metal/<zid>; undeclared on deinit. Keyexpr is a
         * view over a stack token: safe here, the declare copies it. */
        {
            char tok[48];
            z_view_keyexpr_t live;
            uint32_t i;
            int n = 0;
            for (i = 0; i < (uint32_t)PM_METAL_NET_ZENOH_ZID_LEN && n < (int)sizeof(tok) - 1; i++) {
                n += snprintf(tok + n, sizeof(tok) - (size_t)n, "%02x", (unsigned)sl->zid[i]);
            }
            z_view_keyexpr_from_str_unchecked(&live, tok);
            (void)z_liveliness_declare_token(z_loan(*zs), &sl->token, z_loan(live), NULL);
            sl->token_set = 1;
        }
    sl->open_state = 2;
    return 1;
}

int32_t pm_metal_net_zenoh_init(pm_util_mem_arena_t *arena) {
    if (arena == NULL) {
        return -1;
    }
    memset(s_slots, 0, sizeof(s_slots));
    memset(s_exec_in, 0, sizeof(s_exec_in));
    s_arena = arena;
    s_sel = 0;
    s_yield_hook = NULL;
    s_local_zid_init = 0;
    return 0;
}

void pm_metal_net_zenoh_deinit(void) {
    uint8_t i;
    for (i = 0; i < PM_METAL_NET_ZENOH_SLOTS; i++) {
        pm_metal_net_zenoh_ctx_t *sl = &s_slots[i];
        if (sl->sub_set) {
            z_undeclare_subscriber(z_move(sl->sub));
            sl->sub_set = 0;
        }
        if (sl->open_state == 2) {
            (void)z_close(z_loan_mut(sl->session), NULL);
            z_drop(z_move(sl->session));
        }
        if (sl->token_set) {
            (void)z_liveliness_undeclare_token(z_move(sl->token));
            sl->token_set = 0;
        }
        sl->open_state = 0;
    }
    s_yield_hook = NULL;
    s_arena = NULL;
}

int32_t pm_metal_net_zenoh_sel(uint8_t slot) {
    if (slot >= PM_METAL_NET_ZENOH_SLOTS) {
        return -1;
    }
    s_sel = slot;
    return 0;
}

int32_t pm_metal_net_zenoh_peer(uint32_t ip, uint16_t port, uint32_t mode) {
    pm_metal_net_zenoh_ctx_t *sl = &s_slots[s_sel];
    if (ip == 0u) {
        return -1;
    }
    sl->peer_ip = ip;
    sl->peer_port = port;
    sl->mode = (uint8_t)(mode != 0u && mode <= 1u ? 1 : 0);
    sl->open_state = 0;
    return 0;
}

int32_t pm_metal_net_zenoh_up(void) {
    return try_open(&s_slots[s_sel]);
}

int32_t pm_metal_net_zenoh_poll(void) {
    uint8_t i;
    /* Progress any pending open on the selected slot, then advance every open
     * slot so one pump loop makes a listener + connector handshake together. */
    if (s_slots[s_sel].open_state != 2) {
        (void)try_open(&s_slots[s_sel]);
    }
    spin_open_slots();
    if (s_slots[s_sel].open_state == 2) {
        pm_metal_net_zenoh_yield();   /* pump net.ip once more after the spins */
    }
    for (i = 0; i < PM_METAL_NET_ZENOH_SLOTS; i++) {
        (void)try_open(&s_slots[i]);  /* retry slots that were not open yet */
    }
    return 0;
}

int32_t pm_metal_net_zenoh_put(const char *key, const uint8_t *payload, uint32_t plen) {
    z_owned_bytes_t data;
    z_view_keyexpr_t ke;
    z_result_t ret;
    pm_metal_net_zenoh_ctx_t *sl = &s_slots[s_sel];
    if (sl->open_state != 2 || key == NULL || (plen && payload == NULL)) {
        return -1;
    }
    z_view_keyexpr_from_str_unchecked(&ke, key);
    z_bytes_copy_from_buf(&data, payload, plen);
    ret = z_put(z_loan(sl->session), z_loan(ke), z_move(data), NULL);
    z_drop(z_move(data));
    return ret == Z_OK ? 1 : -1;
}

/* zenoh-pico's subscriber callback. `arg` is the slot index (as a small int)
 * since `s_slots` is a static array living for the whole card lifetime. The
 * sample is a loan valid only for this call: key + payload are copied into a
 * bounded stack buffer before the card callback runs, so no pointer is held
 * across a poll() step. */
static void zenoh_sample_cb(_z_sample_t *sample, void *arg) {
    uintptr_t idx = (uintptr_t)arg;
    uint8_t stack[PM_METAL_NET_ZENOH_MAX_SAMPLE];
    z_view_string_t kw;
    const z_loaned_string_t *kl;
    const char *key;
    const z_loaned_bytes_t *pl;
    z_view_slice_t view;
    size_t used = 0;
    if (idx >= (uintptr_t)PM_METAL_NET_ZENOH_SLOTS || sample == NULL) {
        return;
    }
    /* Key: copy into a local view string via the keyexpr accessor. z_loaned_*
     * types are typedefs of the _z_* structs, so `sample` loans directly. */
    key = NULL;
    if (z_keyexpr_as_view_string(z_sample_keyexpr((const z_loaned_sample_t *)sample), &kw) == Z_OK) {
        kl = z_loan(kw);
        key = z_string_data(kl);
    }
    pl = z_sample_payload((const z_loaned_sample_t *)sample);
    /* Payload: iterate the byte slices into the bounded stack buffer. */
    {
        z_bytes_slice_iterator_t it = z_bytes_get_slice_iterator(pl);
        while (used < sizeof(stack) && z_bytes_slice_iterator_next(&it, &view)) {
            const z_loaned_slice_t *s = z_loan(view);
            size_t n = z_slice_len(s);
            if (n > sizeof(stack) - used) {
                n = sizeof(stack) - used;
            }
            memcpy(stack + used, z_slice_data(s), n);
            used += n;
        }
    }
    {
        pm_metal_net_zenoh_ctx_t *sl = &s_slots[idx];
        if (sl->sub_cb != NULL) {
            size_t klen = (key != NULL) ? (size_t)z_string_len(kl) : 0;
            sl->sub_cb(key != NULL ? key : "", klen, stack, used, sl->sub_arg);
        }
    }
}

int32_t pm_metal_net_zenoh_subscribe(const char *key, pm_metal_net_zenoh_value_cb_t cb, void *arg) {
    z_view_keyexpr_t ke;
    z_owned_closure_sample_t cl;
    z_result_t ret;
    pm_metal_net_zenoh_ctx_t *sl = &s_slots[s_sel];
    if (sl->open_state != 2 || key == NULL || cb == NULL) {
        return -1;
    }
    if (sl->sub_set) {
        return -1; /* one subscription per slot for this border pass */
    }
    z_view_keyexpr_from_str_unchecked(&ke, key);
    /* Move a fresh closure (slot index as context) into the declaration; the
     * session owns it and calls zenoh_sample_cb() on delivery inside a spin. */
    (void)z_closure_sample(&cl, zenoh_sample_cb, NULL, (void *)(uintptr_t)s_sel);
    ret = z_declare_subscriber(z_loan(sl->session), &sl->sub, z_loan(ke), z_move(cl), NULL);
    if (ret != Z_OK) {
        return -1;
    }
    sl->sub_set = 1;
    sl->sub_cb = cb;
    sl->sub_arg = arg;
    return 1;
}

int32_t pm_metal_net_zenoh_zid(uint8_t out[PM_METAL_NET_ZENOH_ZID_LEN]) {
    pm_metal_net_zenoh_ctx_t *sl = &s_slots[s_sel];
    if (out == NULL) {
        return -1;
    }
    if (sl->open_state == 2 && sl->zid_set) {
        memcpy(out, sl->zid, PM_METAL_NET_ZENOH_ZID_LEN);
    } else {
        zenoh_local_zid(out);
    }
    return 1;
}

PM_MOD_EXPORT_C(pymergetic.metal.net.zenoh, pm_metal_net_zenoh_init, pm_metal_net_zenoh_init, int32_t(pm_util_mem_arena_t *));
PM_MOD_EXPORT_C(pymergetic.metal.net.zenoh, pm_metal_net_zenoh_deinit, pm_metal_net_zenoh_deinit, void(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.zenoh, pm_metal_net_zenoh_sel, pm_metal_net_zenoh_sel, int32_t(uint8_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.zenoh, pm_metal_net_zenoh_peer, pm_metal_net_zenoh_peer, int32_t(uint32_t, uint16_t, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.zenoh, pm_metal_net_zenoh_up, pm_metal_net_zenoh_up, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.zenoh, pm_metal_net_zenoh_poll, pm_metal_net_zenoh_poll, int32_t(void));
PM_MOD_EXPORT_C(pymergetic.metal.net.zenoh, pm_metal_net_zenoh_put, pm_metal_net_zenoh_put, int32_t(const char *, const uint8_t *, uint32_t));
PM_MOD_EXPORT_C(pymergetic.metal.net.zenoh, pm_metal_net_zenoh_subscribe, pm_metal_net_zenoh_subscribe, int32_t(const char *, pm_metal_net_zenoh_value_cb_t, void *));
PM_MOD_EXPORT_C(pymergetic.metal.net.zenoh, pm_metal_net_zenoh_zid, pm_metal_net_zenoh_zid, int32_t(uint8_t *));

PM_MOD_BOOT_READY_C(pymergetic.metal.net.zenoh, pm_metal_net_zenoh_init, pm_metal_net_zenoh_deinit, NULL);
PM_MOD_BOOTDEP_C(pymergetic.metal.net.zenoh, pymergetic.metal.async);
PM_MOD_BOOTDEP_C(pymergetic.metal.net.zenoh, pymergetic.metal.net.ip);
