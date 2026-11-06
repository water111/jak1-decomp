#include "sce/ee/libkernl/sif.h"

extern int printf(const char*, ...);
extern void sceResetttyinit(void);

static struct {
  SifCommandHeader header;
  int length, mode;
  char arguments[80];
} rdata __attribute__((aligned(64)));

int sceSifResetIop(const char* arguments, int mode) {
  SifDmaTransfer transfer;
  void* destination;
  int length;
  sceSifStopDma();
  destination = (void*)sceSifGetReg(0x80000000);
  rdata.mode = mode;
  for (length = 0; arguments[length]; length++)
    rdata.arguments[length] = arguments[length];
  rdata.length = length;
  rdata.header.destination = 0;
  rdata.header.command = 0x80000003;
  rdata.header.data_size = 0;
  rdata.header.packet_size = sizeof(rdata);
  transfer.source = (unsigned)&rdata;
  transfer.destination = (unsigned)destination;
  transfer.size = sizeof(rdata);
  transfer.attributes = 0x44;
  sceSifWriteBackDCache(&rdata, sizeof(rdata));
  if (sceSifSetDma(&transfer, 1)) {
    sceSifSetReg(4, 0x10000);
    sceSifSetReg(4, 0x20000);
    sceSifSetReg(0x80000002, 0);
    sceSifSetReg(0x80000000, 0);
    return 1;
  }
  return 0;
}

int sceSifIsAliveIop(void) {
  if (sceSifGetReg(4) & 0x10000)
    return 1;
  return 0;
}

int sceSifSyncIop(void) {
  if (sceSifGetReg(4) & 0x40000) {
    sceSifSetReg(4, 0x40000);
    sceResetttyinit();
    return 1;
  }
  return 0;
}

int sceSifRebootIop(const char* image) {
  char arguments[80];
  char* prefix = "rom0:UDNL ";
  char* out;
  const char* end = image;
  while (*end)
    end++;
  if ((unsigned)((long)end - ((long)image - 11)) > sizeof(arguments)) {
    printf("too long parameter '%s'\n", image);
    return 0;
  }
  sceSifInitRpc(0);
  sceSifExitRpc();
  out = arguments;
  while (*prefix)
    *out++ = *prefix++;
  while (*image)
    *out++ = *image++;
  *out = 0;
  return sceSifResetIop(arguments, 0);
}
