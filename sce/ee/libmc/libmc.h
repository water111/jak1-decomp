#ifndef SCE_EE_LIBMC_LIBMC_H
#define SCE_EE_LIBMC_LIBMC_H

#ifdef __cplusplus
extern "C" {
#endif

#include "sce/ee/libkernl/sif.h"

int sceMcInit(void);
SifRpcClient* _lmcGetClientPtr(int** result, int** command);
int sceMcChangeThreadPriority(int priority);
int sceMcGetSlotMax(int port);
int sceMcOpen(int port, int slot, const char* name, int flags);
int sceMcMkdir(int port, int slot, const char* name);
int sceMcClose(int descriptor);
int sceMcSeek(int descriptor, int offset, int origin);
int sceMcRead(int descriptor, void* data, int size);
int sceMcWrite(int descriptor, void* data, int size);
int sceMcSync(int mode, int* command, int* result);
int sceMcGetInfo(int port, int slot, int* type, int* free_blocks, int* formatted);
int sceMcGetDir(int port, int slot, const char* name, unsigned flags, int maximum, void* entries);
int sceMcChdir(int port, int slot, const char* name, char* previous);
int sceMcFormat(int port, int slot);
int sceMcDelete(int port, int slot, const char* name);
int sceMcFlush(int descriptor);
int sceMcSetFileInfo(int port, int slot, const char* name, const char* info, unsigned flags);
int sceMcRename(int port, int slot, const char* name, const char* new_name);
int sceMcUnformat(int port, int slot);
int sceMcGetEntSpace(int port, int slot, const char* name);

enum {
  sceMcExecRun = 0,
  sceMcExecFinish = 1,
  sceMcResSucceed = 0,
  sceMcResNoEntry = -4,
  sceMcResDeniedPermit = -5,
  sceMcResNotEmpty = -6,
  sceMcTypePS2 = 2
};

// Directory results occupy one DMA-aligned cache line.
typedef struct sceMcTblGetDir {
  unsigned char created[8], modified[8];
  unsigned int size;
  unsigned short attributes, reserved1;
  unsigned int reserved2[2];
  char name[32];
} __attribute__((aligned(64))) sceMcTblGetDir;

// PS2 browser metadata: lighting, title, and the three icon filenames.
typedef struct sceMcIconSys {
  char Head[4];
  unsigned short Reserv1, OffsLF;
  unsigned int Reserv2, TransRate;
  int BgColor[4][4];
  float LightDir[3][4], LightColor[3][4], Ambient[4];
  char TitleName[68], FnameView[64], FnameCopy[64], FnameDel[64];
  unsigned char Reserve3[512];
} sceMcIconSys;

#ifdef __cplusplus
}
#endif

#endif
