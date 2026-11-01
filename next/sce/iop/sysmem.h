#ifndef _SYSMEM_H
#define _SYSMEM_H

// System Memory Manager

#define SMEM_Low (0)

extern void *AllocSysMemory(int type, unsigned long size, void *addr);
extern unsigned long QueryTotalFreeMemSize();

#endif
