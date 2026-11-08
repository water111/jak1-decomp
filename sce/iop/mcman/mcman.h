#ifndef SCE_IOP_MCMAN_H
#define SCE_IOP_MCMAN_H

#ifndef MCMAN_VERSION
#define MCMAN_VERSION 0x0215
#endif

#include "sce/iop/loadcore/loadcore.h"
#include "sce/iop/mcman/types.h"

struct McDirectoryEntry {
  struct McDateTime created, modified;
  unsigned int size;
  unsigned short attributes;
  unsigned short reserved;
  unsigned int application_data, entry_index;
  char name[32];
};

int McOpen(int port, int slot, const char* name, int flags);
int McClose(int fd);
int McRead(int fd, void* data, int size);
int McWrite(int fd, void* data, int size);
int McSeek(int fd, int offset, int origin);
int McFormat(int port, int slot);
int McGetDir(int port,
             int slot,
             const char* name,
             int flags,
             int count,
             struct McDirectoryEntry* entries);
int McDelete(int port, int slot, const char* name, int flags);
int McFlush(int fd);
int McChDir(int port, int slot, const char* name, char* current);
int McSetFileInfo(int port, int slot, const char* name, struct McDirectoryEntry* info, int flags);
int McDetectCard2(int port, int slot);
int McGetFormat(int port, int slot);
int McGetEntSpace(int port, int slot, const char* name);
int McReplaceBadBlock(void);
int McSetSio2Mode(int mode);
int McUnformat(int port, int slot);
int McInit(int value);
int McGetFreeClusters(int port, int slot);
int McGetMcType(int port, int slot);
struct ModuleInfo* McGetModuleInfo(void);
int McCheckBlock(int port, int slot, int block);
int McGetSemaphore(void);

#endif
