/* pymergetic.metal.net.rpc — border prove. Exercises handler registration,
 * lookup, invoke/poll lifecycle, edge cases, and the data structures without
 * requiring a running zenoh session. */
#include "pymergetic/metal/net/rpc/__exports__.h"
#include "pymergetic/wasmmod/guest.h" /* PM_MOD_TEST_C */
#include "pymergetic/util/limits.h"
#include "pymergetic/metal/net/rpc.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int32_t fail_rpc(const char *why) {
    fprintf(stderr, "metal.net.rpc test: %s\n", why);
    return 1;
}

/* A test handler that echoes back its args plus a suffix byte. */
static int32_t echo_handler(const uint8_t *args, uint32_t args_len,
    uint8_t *result, uint32_t *result_len, void *user) {
    uint32_t cap;
    (void)user;
    if (result_len == NULL) {
        return -1;
    }
    cap = *result_len;
    if (args_len + 1 > cap) {
        return -1;
    }
    if (args_len > 0 && args != NULL) {
        memcpy(result, args, args_len);
    }
    result[args_len] = 0xEE;
    *result_len = args_len + 1;
    return 0;
}

/* Another handler: always returns error for a specific key. */
static int32_t fail_handler(const uint8_t *args, uint32_t args_len,
    uint8_t *result, uint32_t *result_len, void *user) {
    (void)args;
    (void)args_len;
    (void)result;
    (void)result_len;
    (void)user;
    return -42;
}

static int32_t case_rpc_single(void) {
    pm_metal_rpc_result_t pres;
    uint8_t rbuf[PM_METAL_RPC_RESULT_MAX];
    uint32_t rlen;
    int64_t cid;
    int32_t rc;
    int32_t i;

    /* Misuse guards: NULL key, empty key, NULL handler. */
    if (pm_metal_rpc_register(NULL, echo_handler, NULL) != -1) {
        return fail_rpc("register null key");
    }
    if (pm_metal_rpc_register("", echo_handler, NULL) != -1) {
        return fail_rpc("register empty key");
    }
    if (pm_metal_rpc_register("echo", NULL, NULL) != -1) {
        return fail_rpc("register null handler");
    }
    if (pm_metal_rpc_unregister(NULL) != -1) {
        return fail_rpc("unregister null key");
    }
    if (pm_metal_rpc_unregister("no_such") != -1) {
        return fail_rpc("unregister nonexistent");
    }

    /* Register a handler. */
    if (pm_metal_rpc_register("echo", echo_handler, NULL) != 0) {
        return fail_rpc("register echo");
    }
    if (pm_metal_rpc_register("fail", fail_handler, NULL) != 0) {
        pm_metal_rpc_unregister("echo");
        return fail_rpc("register fail");
    }

    /* Register the same key again: should replace. */
    if (pm_metal_rpc_register("echo", echo_handler, (void *)0x1) != 0) {
        pm_metal_rpc_unregister("echo");
        pm_metal_rpc_unregister("fail");
        return fail_rpc("register replace");
    }

    /* Invoke: NULL/empty key, oversize args. */
    if (pm_metal_rpc_invoke("peer", NULL, (const uint8_t *)"x", 1) != -1) {
        return fail_rpc("invoke null key");
    }
    if (pm_metal_rpc_invoke("peer", "", (const uint8_t *)"x", 1) != -1) {
        return fail_rpc("invoke empty key");
    }
    {
        uint8_t big[PM_METAL_RPC_ARG_MAX + 1];
        memset(big, 0, sizeof(big));
        if (pm_metal_rpc_invoke("peer", "echo", big, PM_METAL_RPC_ARG_MAX + 1) != -1) {
            return fail_rpc("invoke oversize");
        }
    }

    /* Invoke a real call — stores pending slot, gets a call_id. */
    cid = pm_metal_rpc_invoke("peer", "echo", (const uint8_t *)"hello", 5);
    if (cid <= 0) {
        return fail_rpc("invoke echo");
    }

    /* Poll with no result yet: returns 0. */
    if (pm_metal_rpc_poll(&pres) != 0) {
        return fail_rpc("poll no result");
    }

    /* Poll NULL out: error. */
    if (pm_metal_rpc_poll(NULL) != -1) {
        return fail_rpc("poll null out");
    }

    /* Invoke many to fill pending ring. */
    for (i = 0; i < (int32_t)PM_METAL_RPC_CALLS_MAX - 1; i++) {
        cid = pm_metal_rpc_invoke("peer", "echo", (const uint8_t *)"x", 1);
        if (cid <= 0) {
            return fail_rpc("invoke batch fill too soon");
        }
    }
    /* One more should fail: ring full. */
    cid = pm_metal_rpc_invoke("peer", "echo", (const uint8_t *)"x", 1);
    if (cid != (int64_t)-1) {
        return fail_rpc("invoke ring full");
    }

    /* Handle: call the echo handler directly (server-side path). */
    rlen = sizeof(rbuf);
    memset(rbuf, 0, rlen);
    rc = pm_metal_rpc_handle("echo", (const uint8_t *)"abc", 3, rbuf, &rlen);
    if (rc != 0) {
        return fail_rpc("handle echo");
    }
    if (rlen != 4 || memcmp(rbuf, "abc\xEE", 4) != 0) {
        return fail_rpc("handle echo result");
    }

    /* Handle a key with no handler. */
    rlen = sizeof(rbuf);
    rc = pm_metal_rpc_handle("nosuch", (const uint8_t *)"x", 1, rbuf, &rlen);
    if (rc != -2) {
        return fail_rpc("handle no handler");
    }

    /* Handle: NULL args. */
    if (pm_metal_rpc_handle(NULL, (const uint8_t *)"x", 1, rbuf, &rlen) != -1) {
        return fail_rpc("handle null key");
    }
    if (pm_metal_rpc_handle("echo", NULL, 1, rbuf, &rlen) != -1) {
        return fail_rpc("handle null args");
    }

    /* Unregister and re-register. */
    if (pm_metal_rpc_unregister("echo") != 0) {
        return fail_rpc("unregister echo phase2");
    }
    if (pm_metal_rpc_unregister("echo") != -1) {
        return fail_rpc("unregister echo twice");
    }
    if (pm_metal_rpc_handle("echo", (const uint8_t *)"x", 1, rbuf, &rlen) != -2) {
        return fail_rpc("handle after unregister");
    }

    /* Re-register after unregister. */
    if (pm_metal_rpc_register("echo", echo_handler, NULL) != 0) {
        return fail_rpc("re-register echo");
    }
    rlen = sizeof(rbuf);
    rc = pm_metal_rpc_handle("echo", (const uint8_t *)"z", 1, rbuf, &rlen);
    if (rc != 0 || rlen != 2) {
        return fail_rpc("handle after re-register");
    }

    /* Fill all handler slots. */
    {
        char kbuf[16];
        for (i = (int32_t)PM_METAL_RPC_HANDLERS_MAX - 3; i >= 0; i--) {
            snprintf(kbuf, sizeof(kbuf), "h%d", i);
            if (pm_metal_rpc_register(kbuf, echo_handler, NULL) != 0) {
                return fail_rpc("fill handlers");
            }
        }
        /* One more should fail. */
        if (pm_metal_rpc_register("overflow", echo_handler, NULL) != -1) {
            return fail_rpc("handler overflow");
        }
    }

    /* Clean: unregister all. */
    pm_metal_rpc_unregister("fail");
    for (i = 0; i < (int32_t)PM_METAL_RPC_HANDLERS_MAX - 2; i++) {
        char kbuf[16];
        snprintf(kbuf, sizeof(kbuf), "h%d", i);
        (void)pm_metal_rpc_unregister(kbuf);
    }
    (void)pm_metal_rpc_unregister("echo");

    return 0;
}

PM_MOD_TEST_C(pymergetic.metal.net.rpc, case_rpc_single, case_rpc_single);