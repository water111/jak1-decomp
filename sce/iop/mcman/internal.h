#ifndef SCE_IOP_MCMAN_INTERNAL_H
#define SCE_IOP_MCMAN_INTERNAL_H
#include "sce/iop/mcman/mcman.h"
#include "sce/iop/types.h"

int mcman_shutdown(int mode);
int mcman_is_dot_path(const char* path);

#define MCMAN_PORTS 2
#define MCMAN_SLOTS 4
#define MCMAN_HANDLES 3

typedef struct McDeviceInfo {
  char magic[28];
  u_char format_version[12];
  short page_size;
  short pages_per_cluster, pages_per_block, reserved;
  u_int clusters_per_card, allocation_offset, allocation_end;
  u_int root_cluster, backup_block1, backup_block2;
  u_char reserved2[8];
  u_int indirect_fat[32];
  int bad_blocks[32];
  u_char card_type, card_flags;
  u_short reserved3;
  int cluster_size, fat_entries_per_cluster, clusters_per_block;
  int formatted;
  u_int root_cluster_copy;
  u_int unknown[6];
  int free_clusters;
} McDeviceInfo;

typedef struct McFileHandle {
  signed char status;
  signed char write_flag, read_flag, unknown1, directory_flag, unknown2;
  short port, slot, reserved;
  u_int position, size, free_link, link, cluster_offset, cluster;
  u_int entry_index, parent_cluster, parent_entry_index;
} McFileHandle;

extern McFileHandle mcman_handles[MCMAN_HANDLES];
void mcman_invhandles(int port, int slot);
int McCloseAll(void);

extern McDeviceInfo mcman_devices[MCMAN_PORTS][MCMAN_SLOTS];
extern struct ModuleInfo Module;
int mcman_chrpos(const char* text, int character);
void mcman_wmemset(void* buffer, u_int size, int value);
int mcman_calcEDC(const void* buffer, u_int size);
int mcman_checkpath(const char* path);
int mcman_checkdirpath(const char* name, const char* pattern);
int mcman_findfree1(int port, int slot, int reserve);
int mcman_findfree2(int port, int slot, int reserve);
void mcman_initPS2com(void);
void mcman_initPS1PDAcom(void);
void mcman_initcache(void);
int mcman_initdev(void);
#endif
