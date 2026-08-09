#ifndef PYMERGETIC_METAL_DT_H_
#define PYMERGETIC_METAL_DT_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum pm_metal_dt_class {
    PM_METAL_DT_CLASS_TIME = 0,
    PM_METAL_DT_CLASS_GFX = 1,
    PM_METAL_DT_CLASS_AUDIO = 2,
    PM_METAL_DT_CLASS_INPUT = 3,
    PM_METAL_DT_CLASS_FS = 4,
    PM_METAL_DT_CLASS_STREAM = 5,
    PM_METAL_DT_CLASS_NET = 6,
    PM_METAL_DT_CLASS_RANDOM = 7,
    PM_METAL_DT_CLASS_BLK = 8,
    PM_METAL_DT_CLASS_MEM = 9,
    PM_METAL_DT_CLASS_COUNT = 10
} pm_metal_dt_class_t;

typedef enum pm_metal_dt_bus {
    PM_METAL_DT_BUS_PLATFORM = 0,
    PM_METAL_DT_BUS_PCI = 1,
    PM_METAL_DT_BUS_ISA = 2
} pm_metal_dt_bus_t;

enum {
    PM_METAL_DT_CAP_BOUND = 1u
};

typedef struct DtNode {
    pm_metal_dt_class_t class;
    const uint8_t *compat;
    uint32_t unit;
    uint32_t caps;
    pm_metal_dt_bus_t bus;
    uint32_t loc[4];
} DtNode;

void pm_metal_dt_reset(void);
int32_t pm_metal_dt_add(const DtNode *node);
const DtNode *pm_metal_dt_get(uint32_t id);
uint32_t pm_metal_dt_count(void);
uint32_t pm_metal_dt_count_class(pm_metal_dt_class_t class);
const DtNode *pm_metal_dt_by_class(pm_metal_dt_class_t class, uint32_t index);
const DtNode *pm_metal_dt_lookup(pm_metal_dt_class_t class);
int32_t pm_metal_dt_set_compat(pm_metal_dt_class_t class, uint32_t index, const uint8_t *compat);
int32_t pm_metal_dt_or_caps(pm_metal_dt_class_t class, uint32_t index, uint32_t caps);
typedef int32_t (*pm_metal_dt_iter_fn)(const DtNode *node, void *ctx);
void pm_metal_dt_foreach(pm_metal_dt_iter_fn fn, void *ctx);
int32_t pm_metal_dt_seed_mem(const uint8_t *compat, uint32_t caps, uint64_t base, uint64_t size);
int32_t pm_metal_dt_seed_bound_uart(const uint8_t *compat, pm_metal_dt_bus_t bus, uint32_t iobase);
int32_t pm_metal_dt_uart_bound(uint32_t iobase);

#ifdef __cplusplus
}
#endif

#endif /* PYMERGETIC_METAL_DT_H_ */
