#ifndef SCE_IOP_989SND_INIT_H
#define SCE_IOP_989SND_INIT_H

#include "sce/iop/types.h"

struct SoundTimerInfo;

int snd_StartSoundSystem(void);
int snd_InstallSoundTick(void);
int snd_StopSoundSystem(void);
int snd_SystemRunning(void);
void snd_RemoveSoundTick(void);
int snd_GetTickRate(void);
#if SND989_VERSION == 0x020604
int snd_TimerLLTick(struct SoundTimerInfo* timer);
#endif
int snd_TimerLLIRQLoop(void);
extern int gTickMode;
#endif
