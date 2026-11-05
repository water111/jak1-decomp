#ifndef _SYSMEM_H
#define _SYSMEM_H

// System Memory Manager

#define SMEM_Low (0)

extern void* AllocSysMemory(int type, unsigned long size, void* addr);
int FreeSysMemory(void* address);
extern unsigned long QueryTotalFreeMemSize();

extern void* Kprintf(const char* format, ...);

#endif
