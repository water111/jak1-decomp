#ifndef SCE_IOP_989SND_AUTOPAN_H
#define SCE_IOP_989SND_AUTOPAN_H

#include "sce/iop/989snd/effects.h"
void snd_AutoPan(u_int handle, int pan, int direction, int ticks, int delta_from);
int snd_AutoPanHandler(BasicEffect* effect, struct GenericSoundHandler* handler);
#endif
