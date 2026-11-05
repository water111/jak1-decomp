#ifndef SCE_IOP_989SND_PLAYBACK_H
#define SCE_IOP_989SND_PLAYBACK_H

#include "sce/iop/types.h"

struct SoundBank;

u_int snd_PlaySoundVolPanPMPB(struct SoundBank* bank,
                              int index,
                              int volume,
                              int pan,
                              short pitch_modifier,
                              short bend);
u_int snd_SoundIsStillPlaying(u_int handle);
int snd_IsSoundALooper(struct SoundBank* bank, int index);
void snd_PauseSound(u_int handle);
void snd_ContinueSound(u_int handle);
void snd_StopSound(u_int handle);
void snd_StopAllSoundsInBank(struct SoundBank* bank, int silence);
void snd_SetSoundVolPan(u_int handle, int volume, int pan);
void snd_SetSoundPitch(u_int handle, int pitch);
int snd_GetSoundOriginalPitch(struct SoundBank* bank, int index);
int snd_GetSoundCurrentPitch(u_int handle);
void snd_SetSoundPitchBend(u_int handle, short bend);
int snd_GetSoundPitchBend(u_int handle);
void snd_SetSoundPitchModifier(u_int handle, short modifier);
int snd_GetSoundPitchModifier(u_int handle);
#if SND989_VERSION == 0x020604
u_int snd_SetSoundParams(u_int handle,
                         u_int mask,
                         int volume,
                         int pan,

                         short pitch_modifier,
                         short bend

);
#else
u_int snd_SetSoundParams(u_int handle,
                         u_int mask,
                         int volume,
                         int pan,

                         int pitch_modifier,
                         int bend

);
#endif
int snd_CalcSoundNameHash(const char* name);
#endif
