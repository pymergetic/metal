#ifndef PYMERGETIC_METAL_NET_IP_CFG_H_
#define PYMERGETIC_METAL_NET_IP_CFG_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PM_METAL_NET_IP_IFNAME_MAX 8
#define PM_METAL_NET_IP_MAX_IFS 8
#define PM_METAL_NET_TFTP_HOST_MAX 64
#define PM_METAL_NET_IP_BOOT_FILE_MAX 128

typedef struct pm_metal_net_ip_ifcfg {
    char name[PM_METAL_NET_IP_IFNAME_MAX];
    char ip[16];
    char mask[16];
    char gw[16];
    char dns[16];
    char ntp[16];
    char tftp[PM_METAL_NET_TFTP_HOST_MAX];
    char boot_file[PM_METAL_NET_IP_BOOT_FILE_MAX];
    unsigned char mac[6];
    int link_up;
    const char *backend;
} pm_metal_net_ip_ifcfg_t;

unsigned pm_metal_net_ip_if_count(void);
uint32_t pm_metal_net_ip_if_gen(void);
uint32_t pm_metal_net_ip_if_wait(uint32_t since_gen);
int32_t pm_metal_net_ip_if_status_index(uint32_t index, char *buf, uint32_t buf_len);
int pm_metal_net_ip_if_get_index(unsigned index, pm_metal_net_ip_ifcfg_t *out);
int pm_metal_net_ip_if_get_named(const char *name, pm_metal_net_ip_ifcfg_t *out);
int pm_metal_net_ip_if_get(pm_metal_net_ip_ifcfg_t *out);
int pm_metal_net_ip_if_set_named(const char *name, const char *ip, const char *mask, const char *gw,
                                 const char *dns);
int pm_metal_net_ip_if_set(const char *ip, const char *mask, const char *gw, const char *dns);
int pm_metal_net_ip_if_set_dhcp_named(const char *name);
int pm_metal_net_ip_if_set_dhcp(void);
int pm_metal_net_ip_if_status(char *buf, uint32_t buf_len);
int pm_metal_net_ip_if_status_named(const char *name, char *buf, uint32_t buf_len);
int pm_metal_net_ip_if_boot_get(const char *name, char *tftp_host, uint32_t tftp_cap,
                                char *boot_file, uint32_t boot_cap);
int pm_metal_net_ip_resolve_ip4(const char *host, uint32_t *out_host);
int pm_metal_net_ip_dns_last_ntoa(char *out, uint32_t out_cap);

#ifdef __cplusplus
}
#endif

#endif
