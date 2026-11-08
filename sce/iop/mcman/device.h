#ifndef SCE_IOP_MCMAN_DEVICE_H
#define SCE_IOP_MCMAN_DEVICE_H
#include "sce/iop/mcman/directory.h"
#include "sce/iop/types.h"

struct McIoFile {
  int mode;
  u_int unit;
  void* device;
  int card_fd;
};

struct McIoStat {
  u_int mode, attributes, size;
  struct McDateTime created, accessed, modified;
  u_int reserved[7];
};

struct McIoDirEntry {
  struct McIoStat stat;
  char name[256];
  u_int private_data;
};

struct McIoCard {
  int port, slot;
};
extern struct McIoCard mcman_io_card;
extern int mcman_io_semaphore;
int mcman_ioerrcode(int result);
int mcman_shutdown_device(void);
void mcman_unit2card(u_int unit);
int mcman_dread(int fd, struct McIoDirEntry* entry);
int mcman_getstat(int port, int slot, const char* path, struct McIoStat* info);
int mcman_rename(int port, int slot, const char* old_path, const char* new_path);
int mcman_dread1(int fd, struct McIoDirEntry* output);

int mcman_dread2(int fd, struct McIoDirEntry* output);

int mcman_getstat1(int port, int slot, const char* name, struct McIoStat* output);

int mcman_getstat2(int port, int slot, const char* name, struct McIoStat* output);

int mcman_rename2(int port, int slot, const char* source, const char* target);

#endif
