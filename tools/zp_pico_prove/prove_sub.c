/*
 * Standalone zenoh-pico put/subscriber loopback proof (Stage 1) -- subscriber.
 *
 * Listener half of the proof. Declares the subscriber keyexpr "demo/example"
 * with the multi-segment wildcard suffix (the glob the publisher writes to;
 * see the z_view_keyexpr_from_str string below) and keeps a bounded
 * zp_spin_once + zp_send_keep_alive loop running until prove_pub delivers the
 * expected count, then exits 0 on success / non-zero on failure.
 */
#include <stdio.h>
#include <stdlib.h>

#include <zenoh-pico.h>

#define EXPECTED 5

static int g_n = 0;
static void h(z_loaned_sample_t *sample, void *arg) {
    (void)arg;
    z_owned_string_t v;
    z_bytes_to_string(z_sample_payload(sample), &v);
    printf("SUB received '%s'\n", z_string_data(z_string_loan(&v)));
    z_string_drop(z_string_move(&v));
    g_n++;
}

int main(int argc, char **argv) {
    const char *mode = "peer";
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
        printf("SUB open FAIL\n");
        return 1;
    }
    z_owned_closure_sample_t cl;
    z_closure_sample(&cl, h, NULL, NULL);
    z_owned_subscriber_t sub;
    z_view_keyexpr_t vke;
    z_view_keyexpr_from_str(&vke, "demo/example/**");
    if (z_declare_subscriber(z_session_loan(&s), &sub, z_view_keyexpr_loan(&vke), z_closure_sample_move(&cl), NULL) <
        0) {
        printf("SUB declare FAIL\n");
        return 1;
    }

    for (int i = 0; i < 200000 && g_n < EXPECTED; i++) {
        z_sleep_ms(10);
        zp_spin_once(z_session_loan(&s));
        zp_send_keep_alive(z_session_loan(&s), NULL);
    }
    printf("SUB got %d msgs %s\n", g_n, g_n >= EXPECTED ? "OK" : "FAIL");
    z_subscriber_drop(z_subscriber_move(&sub));
    z_session_drop(z_session_move(&s));
    return g_n >= EXPECTED ? 0 : 1;
}
