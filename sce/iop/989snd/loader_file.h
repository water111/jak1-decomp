#ifndef SCE_IOP_989SND_LOADER_FILE_H
#define SCE_IOP_989SND_LOADER_FILE_H

#include "sce/iop/types.h"

struct SndFileChunk {
  int offset;
  int size;
};

struct SndFileHeader {
  u_int type;
  u_int chunk_count;
  struct SndFileChunk chunks[1];
};

extern char gFileLoadBuffer[];
#endif
