#ifndef SCE_IOP_SIFCMD_H
#define SCE_IOP_SIFCMD_H
#include "sce/iop/types.h"
#define SIF_CMD_SET_SYSTEM_REGISTER 0x80000001u

typedef struct {
  u_int size;
  void* destination;
  u_int command;
  u_int option;
} sceSifCmdHeader;

int sceSifSendCmd(u_int command, void* packet, int packet_size, void* src, void* dest, int size);
#endif
