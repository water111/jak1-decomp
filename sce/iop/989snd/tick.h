#ifndef SCE_IOP_989SND_TICK_H
#define SCE_IOP_989SND_TICK_H

#include "sce/iop/types.h"

int snd_SoundTickHandler(void);
unsigned int snd_GetTick(void);
void snd_LockMasterTick(void);
void snd_UnlockMasterTick(void);
void snd_SetTick(unsigned int value);

extern int gLockMasterTick;
extern int gVoiceAllocatorInUse;
extern int gTickAttentionNeeded;
#endif
