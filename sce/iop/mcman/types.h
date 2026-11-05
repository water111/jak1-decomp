#ifndef SCE_IOP_MCMAN_TYPES_H
#define SCE_IOP_MCMAN_TYPES_H
#include "sce/iop/types.h"

struct McDateTime {
  u_char reserved, second, minute, hour, day, month;
  u_short year;
} __attribute__((aligned(4)));

#endif
