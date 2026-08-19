/*
 * Standalone zenoh-pico put/subscriber loopback proof (Stage 1).
 *
 * Publisher half of the proof. Built together with the vendored zenoh-pico
 * unix platform (single-threaded, Z_FEATURE_MULTI_THREAD=0) and run against
 * prove_sub on tcp/127.0.0.1:PORT. Deliberately tiny: this proves only that
 * the vendored lib builds and delivers unicast pub/sub on lo, before any
 * Metal card wraps it. The Metal card (pymergetic.metal.net.zenoh) uses the
 * same zp_spin_once + zp_send_keep_alive flush model in its poll() step.
 */
#include <stdio.h>
#include <stdlib.h>

#include <zenoh-pico.h>

int main(int argc, char **argv) {
    const char *mode = "client";
    const char *key = "demo/example/zenoh-pico-pub";
    const char *cloc = NULL, *lloc = NULL;
    for (int i = 1; i < argc; i++) {
        if (i + 1 < argc && argv[i][0] == '-' && argv[i][1] == 'e') cloc = argv[++i];
        else if (i + 1 < argc && argv[i][0] == '-' && argv[i][1] == 'l') lloc = argv[++i];
        else if (i + 1 < argc && argv[i][0] == '-' && argv[i][1] == 'm') mode = argv[++i];
    }

    z_owned_config_t cfg;
    z_config_default(&cfg);
    zp_config_insert(z_config_loan_mut(&cfg), Z_CONFIG_MODE_KEY, mode);
    if (cloc) zp_config_insert(z_config_loan_mut(&cfg), Z_CONFIG_CONNECT_KEY, cloc);
    if (lloc) zp_config_insert(z_config_loan_mut(&cfg), Z_CONFIG_LISTEN_KEY, lloc);

    z_owned_session_t s;
    if (z_open(&s, z_config_move(&cfg), NULL) < 0) {
        printf("PUB open FAIL\n");
        return 1;
    }
    z_owned_keyexpr_t ke;
    z_view_keyexpr_t vke;
    z_view_keyexpr_from_str(&vke, key);
    if (z_declare_keyexpr(z_session_loan(&s), &ke, z_view_keyexpr_loan(&vke)) < 0) {
        printf("PUB ke FAIL\n");
        return 1;
    }

    for (int i = 0; i < 5; i++) {
        z_owned_bytes_t b;
        z_bytes_from_static_str(&b, "hello-zenoh");
        if (z_put(z_session_loan(&s), z_keyexpr_loan(&ke), z_bytes_move(&b), NULL) < 0) {
            printf("pub put fail %d\n", i);
        }
        /* flush the tx batch so the peer actually receives it */
        z_sleep_ms(20);
        zp_spin_once(z_session_loan(&s));
        zp_send_keep_alive(z_session_loan(&s), NULL);
        printf("PUB put %d done\n", i);
        z_sleep_ms(300);
        for (int k = 0; k < 10; k++) {
            zp_spin_once(z_session_loan(&s));
        }
    }
    z_undeclare_keyexpr(z_session_loan(&s), z_keyexpr_move(&ke));
    z_session_drop(z_session_move(&s));
    return 0;
}
