#ifndef _PM_METAL_TLSF_STDDEF_H_
#define _PM_METAL_TLSF_STDDEF_H_

typedef unsigned long size_t;
typedef long          ptrdiff_t;
#define NULL ((void *)0)
#define offsetof(type, member) ((size_t)&(((type *)0)->member))

#endif
