#ifndef SCE_IOP_989SND_AUTOPITCH_H
#define SCE_IOP_989SND_AUTOPITCH_H

#include "sce/iop/989snd/effects.h"
void snd_AutoPitch(u_int handle, int pitch, int ticks, int delta_from);
int snd_AutoPitchHandler(BasicEffect* effect, struct GenericSoundHandler* handler);
#endif
