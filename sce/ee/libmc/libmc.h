#ifndef SCE_EE_LIBMC_LIBMC_H
#define SCE_EE_LIBMC_LIBMC_H

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

#endif
