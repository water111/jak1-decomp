#ifndef SCE_IOP_989SND_REVERB_H
#define SCE_IOP_989SND_REVERB_H

#include "sce/iop/types.h"

#if SND989_VERSION == 0x020604
int snd_SetReverbType_L(int core, int type);
#else
int snd_SetReverbType_L(int core, int type, int delay, int feedback);
#endif
void snd_CheckVerb(void);
void snd_SetReverbType(int cores, int type);
void snd_SetReverbDepth(int cores, int left, int right);
void snd_SetReverbDepth_L(int core, int left, int right);
#if SND989_VERSION == 0x020608
void snd_PreAllocReverbWorkArea(int cores, int type);
void snd_PreAllocReverbWorkArea_L(int core, int type);
void snd_SetReverbEx(int cores, int type, int depth, int delay, int feedback);
#endif
extern int gReverbType[2];
extern u_int gRevRamStart[2];
extern short gReverbDepthL[2], gReverbDepthR[2];
extern int gAwaitingReverbChange[2], gAwaitingReverbChangeTick[2];
extern u_int gRevRamUse[10];
#if SND989_VERSION == 0x020608
extern int gRevRamPreAllocType[2];
#endif
#endif
