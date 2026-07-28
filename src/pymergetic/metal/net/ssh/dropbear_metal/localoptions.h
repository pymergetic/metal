/* Metal Dropbear feature trim — password + ed25519/curve25519/chacha/aes. */
#ifndef DROPBEAR_METAL_LOCALOPTIONS_H_
#define DROPBEAR_METAL_LOCALOPTIONS_H_

#define DROPBEAR_METAL 1

#define DROPBEAR_DEFPORT    "22"
#define NON_INETD_MODE      0
#define INETD_MODE          1
#define DROPBEAR_REEXEC     0
#define DEBUG_TRACE         0
#define DROPBEAR_SMALL_CODE 1

#define DROPBEAR_X11FWD             0
#define DROPBEAR_CLI_LOCALTCPFWD    0
#define DROPBEAR_CLI_REMOTETCPFWD   0
#define DROPBEAR_SVR_LOCALTCPFWD    0
#define DROPBEAR_SVR_REMOTETCPFWD   0
#define DROPBEAR_SVR_LOCALSTREAMFWD 0
#define DROPBEAR_SVR_AGENTFWD       0
#define DROPBEAR_CLI_AGENTFWD       0
#define DROPBEAR_CLI_PROXYCMD       0
#define DROPBEAR_CLI_NETCAT         0
#define DROPBEAR_USER_ALGO_LIST     0

#define DROPBEAR_AES128           1
#define DROPBEAR_AES256           0
#define DROPBEAR_3DES             0
#define DROPBEAR_CHACHA20POLY1305 1
#define DROPBEAR_ENABLE_CTR_MODE  1
#define DROPBEAR_ENABLE_CBC_MODE  0
#define DROPBEAR_ENABLE_GCM_MODE  0

#define DROPBEAR_SHA1_HMAC     1
#define DROPBEAR_SHA2_256_HMAC 1
#define DROPBEAR_SHA2_512_HMAC 0
#define DROPBEAR_SHA1_96_HMAC  0

#define DROPBEAR_RSA      0
#define DROPBEAR_RSA_SHA1 0
#define DROPBEAR_DSS      0
#define DROPBEAR_ECDSA    0
#define DROPBEAR_ED25519  1
#define DROPBEAR_SK_KEYS  0

#define DROPBEAR_DH_GROUP14_SHA1   0
#define DROPBEAR_DH_GROUP14_SHA256 0
#define DROPBEAR_DH_GROUP16        0
#define DROPBEAR_CURVE25519        1
#define DROPBEAR_ECDH              0
#define DROPBEAR_DH_GROUP1         0

#define DO_HOST_LOOKUP 0
#define DO_MOTD        0

#define DROPBEAR_SVR_PASSWORD_AUTH  1
#define DROPBEAR_SVR_PAM_AUTH       0
#define DROPBEAR_SVR_PUBKEY_AUTH    1
#define DROPBEAR_SVR_PUBKEY_OPTIONS 0
#define DROPBEAR_SVR_MULTIUSER      1
/* DROPBEAR_SIGNKEY_VERIFY is derived in sysoptions.h from SVR_PUBKEY_AUTH. */

#define DROPBEAR_CLI_PASSWORD_AUTH  0
#define DROPBEAR_CLI_PUBKEY_AUTH    0
#define DROPBEAR_USE_SSH_CONFIG     0
#define DROPBEAR_USE_PASSWORD_ENV   0
#define DROPBEAR_CLI_ASKPASS_HELPER 0
#define DROPBEAR_CLI_IMMEDIATE_AUTH 0

#define DROPBEAR_DELAY_HOSTKEY 1
/* Must stay undefined — Dropbear uses #ifndef DISABLE_SYSLOG, not #if. */
#undef DISABLE_SYSLOG
#define DROPBEAR_SFTPSERVER 0
#define DROPBEAR_ONEX       0
#define DROPBEAR_VFORK      0

#define DSS_PRIV_FILENAME     "/etc/ssh/dropbear_dss_host_key"
#define RSA_PRIV_FILENAME     "/etc/ssh/dropbear_rsa_host_key"
#define ECDSA_PRIV_FILENAME   "/etc/ssh/dropbear_ecdsa_host_key"
#define ED25519_PRIV_FILENAME "/etc/ssh/dropbear_ed25519_host_key"

#define DROPBEAR_PIDFILE   "/var/run/dropbear.pid"
#define MAX_UNAUTH_PER_IP  3
#define MAX_UNAUTH_CLIENTS 8
#define MAX_AUTH_TRIES     6
#define UNAUTH_CLOSE_DELAY 0

#define DEFAULT_RECV_WINDOW   (64 * 1024)
#define TRANS_MAX_PAYLOAD_LEN (32 * 1024)
#define RECV_MAX_PAYLOAD_LEN  (32 * 1024)

#endif
