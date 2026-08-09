/* Nested builtin module objects — path == pymergetic.metal.<…> */
#ifndef PYMERGETIC_METAL_GLUE_MODULES_H_
#define PYMERGETIC_METAL_GLUE_MODULES_H_

#include "py/obj.h"

#if !defined(PM_METAL_CFG_FW_BROWSER) || !PM_METAL_CFG_FW_BROWSER
extern const mp_obj_module_t mp_module_pymergetic;
#endif
extern const mp_obj_module_t mp_module_pymergetic_metal;
extern const mp_obj_module_t mp_module_pymergetic_metal_util;
extern const mp_obj_module_t mp_module_pymergetic_metal_net;
extern const mp_obj_module_t mp_module_pymergetic_metal_externals;
extern const mp_obj_module_t mp_module_pymergetic_metal_auth;
extern const mp_obj_module_t mp_module_pymergetic_metal_trust;
extern const mp_obj_module_t mp_module_pymergetic_metal_util_lz4;
extern const mp_obj_module_t mp_module_pymergetic_metal_util_size;
extern const mp_obj_module_t mp_module_pymergetic_metal_util_endian;
extern const mp_obj_module_t mp_module_pymergetic_metal_util_fourcc;
extern const mp_obj_module_t mp_module_pymergetic_metal_util_eightcc;
#if !defined(PM_METAL_CFG_FW_BROWSER) || !PM_METAL_CFG_FW_BROWSER
extern const mp_obj_module_t mp_module_pymergetic_metal_util_tar;
extern const mp_obj_module_t mp_module_pymergetic_metal_net_ip;
extern const mp_obj_module_t mp_module_pymergetic_metal_net_wg;
extern const mp_obj_module_t mp_module_pymergetic_metal_net_ssh;
#endif

#endif
