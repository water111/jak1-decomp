#ifndef SCE_IOP_MCMAN_FILESYSTEM_H
#define SCE_IOP_MCMAN_FILESYSTEM_H
#include "sce/iop/mcman/mcman.h"
#define MCMAN_DENIED (-5)
#define MCMAN_UNFORMATTED (-2)
#define MCMAN_NO_ENTRY (-4)
#define MCMAN_CARD_PS2 2
#define MCMAN_SEEK_SET 0
#define MCMAN_SEEK_CUR 1
#define MCMAN_SEEK_END 2
extern int mcman_allow_ps1;
int mcman_detectcard(int port, int slot);
int mcman_clearcache(int port, int slot);
int McFlushCache(int port, int slot);
int mcman_open1(int port, int slot, const char* path, int flags);
int mcman_open2(int port, int slot, const char* path, int flags);
int mcman_close1(int fd);
int mcman_close2(int fd);
int mcman_read1(int fd, void* buffer, int size);
int mcman_read2(int fd, void* buffer, int size);
int mcman_write1(int fd, void* buffer, int size);
int mcman_write2(int fd, void* buffer, int size);
int mcman_getdir1(int port,
                  int slot,
                  const char* path,
                  int flags,
                  int count,
                  struct McDirectoryEntry* entries);
int mcman_getdir2(int port,
                  int slot,
                  const char* path,
                  int flags,
                  int count,
                  struct McDirectoryEntry* entries);
int mcman_getentspace(int port, int slot, const char* path);
int mcman_setinfo1(int port, int slot, const char* path, struct McDirectoryEntry* info, int flags);
int mcman_setinfo2(int port, int slot, const char* path, struct McDirectoryEntry* info, int flags);
int mcman_chdir(int port, int slot, const char* path, char* current);
int mcman_delete1(int port, int slot, const char* path, int flags);
int mcman_delete2(int port, int slot, const char* path, int flags);
int mcman_format1(int port, int slot);
int mcman_format2(int port, int slot);
int mcman_unformat1(int port, int slot);
int mcman_unformat2(int port, int slot);
int mcman_probePS2Card(int port, int slot);
int mcman_probePS2Card2(int port, int slot);
int mcman_probePS1Card(int port, int slot);
int mcman_probePS1Card2(int port, int slot);
int mcman_probePDACard(int port, int slot);
int mcman_setPS2devinfos(int port, int slot);

int mcman_setPS1devinfos(int port, int slot);

#endif
