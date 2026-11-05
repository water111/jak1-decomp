#ifndef SCE_IOP_MCMAN_BUFFERS_H
#define SCE_IOP_MCMAN_BUFFERS_H
#include "sce/iop/types.h"

union McPageBuffer {
  u_int word[264];
  u_char byte[1056];
};
extern union McPageBuffer mcman_pagebuf;
// Raw transfers also use a byte-aligned view of the shared page workspace.
extern u_char mcman_pagebytes[1056] __asm__("mcman_pagebuf");
extern u_char mcman_eccdata[512];
extern void* mcman_pagedata[32];
#endif
