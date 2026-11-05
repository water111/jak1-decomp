#ifndef SCE_IOP_989SND_AUTOPITCHBEND_H
#define SCE_IOP_989SND_AUTOPITCHBEND_H

#include "sce/iop/989snd/effects.h"
void snd_AutoPitchBend(u_int handle, int pitch, int ticks, int delta_from);
int snd_AutoPitchBendHandler(BasicEffect* effect, struct GenericSoundHandler* handler);
#endif
