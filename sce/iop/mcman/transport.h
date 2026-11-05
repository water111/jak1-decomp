#ifndef SCE_IOP_MCMAN_TRANSPORT_H
#define SCE_IOP_MCMAN_TRANSPORT_H
#include "sce/iop/types.h"
int McReadPage(int port, int slot, int page, void* buffer);
int McWritePage(int port, int slot, int page, const void* buffer, const void* ecc);
int McReadPS1PDACard(int port, int slot, int page, void* buffer);
int McWritePS1PDACard(int port, int slot, int page, const void* buffer);
int mcman_eraseblock(int port, int slot, int block, void** pages, void* ecc);
void McDataChecksum(const void* data, u_char* ecc);
int mcman_PS1pagetest(int port, int slot, int page);
int mcman_spare_size(int port, int slot);
int mcman_fillPS1backuparea(int port, int slot, int page);
int mcman_replaceBackupBlock(int port, int slot, int block);
int mcman_fillbackupblock1(int port, int slot, int block, void** pages, void* ecc);
struct Sio2Transfer;
void mcman_begin_transfer(int port, int slot);
void mcman_end_transfer(void);
int mcman_security_command(int port, int slot, struct Sio2Transfer* packet);
int mcman_getcnum(int port, int slot);
void mcman_build_command(int port, int slot, int command, void* argument);

int mcman_cardchanged(int port, int slot);

int mcman_correctECC(void* data, const u_char* ecc);

int McGetCardSpec(int port,
                  int slot,
                  short* page_size,
                  short* block_size,
                  int* card_size,
                  u_char* flags);

int mcman_readpage(int port, int slot, int page, void* buffer, void* ecc);

#endif
