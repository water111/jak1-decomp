#ifndef SCE_IOP_989SND_AUTOVOL_H
#define SCE_IOP_989SND_AUTOVOL_H

#include "sce/iop/989snd/effects.h"
void snd_AutoVol(u_int handle, int volume, int ticks, int delta_from);
int snd_AutoVolHandler(BasicEffect* effect, struct GenericSoundHandler* handler);
#endif
