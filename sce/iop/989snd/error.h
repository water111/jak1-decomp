#ifndef SCE_IOP_989SND_ERROR_H
#define SCE_IOP_989SND_ERROR_H

#include "sce/iop/types.h"

typedef void (*SndErrorDisplayFunc)(int number, u_int arg1, u_int arg2, u_int arg3, u_int arg4);
void snd_RegisterErrorDisplayFunc(SndErrorDisplayFunc function);

void snd_ShowError(int number, u_int arg1, u_int arg2, u_int arg3, u_int arg4);

#endif
