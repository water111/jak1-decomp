#ifndef SCE_IOP_PADMAN_RUNTIME_H
#define SCE_IOP_PADMAN_RUNTIME_H
#include "sce/iop/padman/threads.h"

typedef struct PadRuntime {
  PadVblankState vblank;
  int reserved;
  int initialized;
  void* ee_buffer;
  int high_priority, low_priority;
  int reserved34;
} PadRuntime;

extern PadRuntime pad_runtime;
#endif
