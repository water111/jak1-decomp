#ifndef SCE_IOP_989SND_EXTERN_PROC_H
#define SCE_IOP_989SND_EXTERN_PROC_H

#include "sce/iop/types.h"

typedef int (*Extern989Proc)(int, int, int, int, int);

struct Extern989Handler {
  u_int proc_id;
  int num_funcs;
  struct Extern989Handler* next;
  u_int reserved;
  Extern989Proc procs[1];
};

int snd_RegisterExternProcHandler(struct Extern989Handler* handler);
int snd_UnregisterExternProcHandler(struct Extern989Handler* handler);
struct Extern989Handler* snd_FindExternProcHandler(u_int id);
int snd_DoExternCall(u_int id, int index, int arg1, int arg2, int arg3, int arg4, int arg5);
#endif
