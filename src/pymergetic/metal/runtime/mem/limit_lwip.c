/** @file
  Compile-time mem/buffer limit rows for lwIP opts (net.ip).
**/
#include <pymergetic/metal/runtime/mem/limit.h>

/* Path-relative: this TU is under runtime/mem/, not on the net/ip -I. */
#include "../../net/ip/lwipopts.h"

PM_METAL_MEM_LIMIT(g_pm_metal_lim_net_ip_MEM_SIZE, "net.ip", "MEM_SIZE", MEM_SIZE, "bytes", "lwIP heap (MEM_SIZE)");
PM_METAL_MEM_LIMIT(g_pm_metal_lim_net_ip_PBUF_POOL_SIZE, "net.ip", "PBUF_POOL_SIZE", PBUF_POOL_SIZE, "count", "lwIP pbuf pool entries");
PM_METAL_MEM_LIMIT(g_pm_metal_lim_net_ip_PBUF_POOL_BUFSIZE, "net.ip", "PBUF_POOL_BUFSIZE", PBUF_POOL_BUFSIZE, "bytes", "lwIP pbuf pool buffer size");
PM_METAL_MEM_LIMIT(g_pm_metal_lim_net_ip_TCP_WND, "net.ip", "TCP_WND", TCP_WND, "bytes", "lwIP TCP receive window");
PM_METAL_MEM_LIMIT(g_pm_metal_lim_net_ip_TCP_SND_BUF, "net.ip", "TCP_SND_BUF", TCP_SND_BUF, "bytes", "lwIP TCP send buffer");
PM_METAL_MEM_LIMIT(g_pm_metal_lim_net_ip_MEMP_NUM_TCP_PCB, "net.ip", "MEMP_NUM_TCP_PCB", MEMP_NUM_TCP_PCB, "count", "lwIP active TCP PCB pool");
PM_METAL_MEM_LIMIT(g_pm_metal_lim_net_ip_MEMP_NUM_TCP_SEG, "net.ip", "MEMP_NUM_TCP_SEG", MEMP_NUM_TCP_SEG, "count", "lwIP TCP segment pool");
