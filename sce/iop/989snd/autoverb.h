#ifndef SCE_IOP_989SND_AUTOVERB_H
#define SCE_IOP_989SND_AUTOVERB_H

#include "sce/iop/types.h"

void snd_AutoReverb_L(int core, int depth, int delta_time, int channel_flags);
void snd_AutoVerbHandler(void);
void snd_AutoReverb(int core, int depth, int delta_time, int channel_flags);
void snd_KillAutoVerbForCore(int core);
#endif
