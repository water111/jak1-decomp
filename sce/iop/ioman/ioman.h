#ifndef SCE_IOP_IOMAN_H
#define SCE_IOP_IOMAN_H

#include "sce/iop/types.h"

#define IOP_O_RDONLY 1
#define IOP_SEEK_SET 0
#define IOP_SEEK_CUR 1
int open(const char* name, int flags, ...);
int read(int descriptor, void* buffer, size_t count);
int close(int descriptor);
int lseek(int descriptor, int offset, int origin);
#endif
