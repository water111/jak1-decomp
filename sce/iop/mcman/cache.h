#ifndef SCE_IOP_MCMAN_CACHE_H
#define SCE_IOP_MCMAN_CACHE_H
#include "sce/iop/types.h"

struct McFileHandle;

struct McCacheEntry {
  int cluster;
  u_char* data;
  short slot;
  signed char dirty, port;
  signed char read_flag;
  u_char reserved[3];
};

struct McFatCache {
  int entries[513];
};

extern int mcman_cache_count;
extern struct McCacheEntry* mcman_cache_entries;
extern struct McCacheEntry** mcman_cache_order;
extern struct McFatCache mcman_fat_cache[2][4];
struct McCacheEntry* mcman_getcacheentry(int port, int slot, int cluster);
void mcman_freecluster(int port, int slot, int cluster);
int mcman_getFATindex(int port, int slot, int index);
void Mc1stCacheEntSetWrFlagOff(void);
struct McCacheEntry* mcman_get1stcacheEntp(void);
void mcman_addcacheentry(struct McCacheEntry* entry);
int mcman_flushcacheentry(struct McCacheEntry* entry);
int McReadCluster(int port, int slot, int cluster, struct McCacheEntry** entry);
int mcman_readclusterPS1(int port, int slot, int cluster, struct McCacheEntry** entry);
int McGetFATentry(int port, int slot, int index, int* value);
int McSetFATentry(int port, int slot, int index, int value);
int mcman_cachePS1dirs(int port, int slot);

int mcman_fatRseekPS1(int fd);

int mcman_fatWseekPS1(int fd);

int mcman_allocPS1cluster(int port, int slot, struct McFileHandle* handle);

int mcman_fatRseek(int fd);

int mcman_fatWseek(int fd);

#endif
