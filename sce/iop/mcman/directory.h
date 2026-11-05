#ifndef SCE_IOP_MCMAN_DIRECTORY_H
#define SCE_IOP_MCMAN_DIRECTORY_H
#include "sce/iop/mcman/types.h"

struct McFsEntry {
  u_short mode, reserved;
  u_int length;
  struct McDateTime created;
  u_int cluster, parent_entry;
  struct McDateTime modified;
  u_int attributes;
  u_char reserved2[28];
  char name[32];
  u_char reserved3[416];
};

struct McPs1Entry {
  u_int mode;
  int length;
  short next_block;
  char name[20];
  u_char unknown1[2];
  struct McDateTime created;
  u_int unknown2;
  u_short unknown3, reserved2;
  struct McDateTime modified;
  u_int unknown4;
  u_char reserved[65];
  u_char transient, unknown5, checksum;
};

struct McDirectoryCache {
  int cluster, entry_index, maximum_entries;
  u_int reserved;
};

int mcman_cachedirentry(int port,
                        int slot,
                        const char* path,
                        struct McDirectoryCache* cache,
                        struct McFsEntry** entry,
                        int mode);
int mcman_getmcrtime(struct McDateTime* time);
int McReadDirEntry(int port, int slot, int cluster, int index, struct McFsEntry** entry);
int mcman_readdirentryPS1(int port, int slot, int cluster, struct McPs1Entry** entry);
int mcman_lookup1(int port, int slot, const char* name, struct McPs1Entry** entry, int mode);

int mcman_setPS1FileState(int port, int slot, int cluster, int flags);

int McCreateDirentry(int port,
                     int slot,
                     int parent_cluster,
                     int parent_index,
                     int cluster,
                     const void* time);

int mcman_getdirinfo(int port,
                     int slot,
                     struct McFsEntry* directory,
                     const char* name,
                     struct McDirectoryCache* cache,
                     int mode);

int McSetDirEntryState(int port, int slot, int cluster, int index, int exists);

#endif
