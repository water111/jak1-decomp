#ifndef SCE_IOP_989SND_EXTERN_PROC_INTERNAL_H
#define SCE_IOP_989SND_EXTERN_PROC_INTERNAL_H

#include "sce/iop/989snd/extern_proc.h"

extern struct Extern989Handler* gExternHandlersList;

static inline struct Extern989Handler* find_handler(u_int id) {
  struct Extern989Handler* work;
  for (work = gExternHandlersList; work; work = work->next) {
    if (work->proc_id == id)
      return work;
  }
  return work;
}

#endif
