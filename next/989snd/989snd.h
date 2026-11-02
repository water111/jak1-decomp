#ifndef SND989_H
#define SND989_H

int snd_PlaySoundVolPanPMPB(void *bank, int index, int volume, int pan, int pitch, int bend);
void snd_SetSoundVolPan(int handle, int volume, int pan);
void snd_SetMIDIRegister(int handle, int reg, short value);
void snd_SetSoundPitchModifier(int handle, int pitch);
void snd_SetSoundPitchBend(int handle, int bend);
void snd_PauseSound(int handle);
void snd_StopSound(int handle);
void snd_ContinueSound(int handle);
void snd_AutoPitch(int handle, int pitch, int time, int from);
void snd_AutoPitchBend(int handle, int bend, int time, int from);
void snd_PauseAllSoundsInGroup(unsigned char group);
void snd_ContinueAllSoundsInGroup(unsigned char group);
void snd_SetMasterVolume(int group, int volume);
void snd_StopSoundSystem(void);
void snd_UnloadBank(void *bank);
void snd_ResolveBankXREFS(void);
int snd_GetVoiceStatus(int voice);
int snd_StartSoundSystem(void);
void snd_RegisterIOPMemAllocator(void *(*alloc)(int, int, int *), void (*free)(void *));
void snd_LockVoiceAllocator(int owner);
int snd_ExternVoiceAlloc(int group, int priority);
void snd_UnlockVoiceAllocator(void);
int snd_SRAMMalloc(int size);
void snd_SetMixerMode(int channel, int reverb);
void snd_SetGroupVoiceRange(int group, int first, int last);
void snd_SetReverbDepth(int cores, int left, int right);
void snd_SetReverbType(int cores, int type);
void snd_SetPanTable(short *table);
void snd_SetPlaybackMode(int mode);
int snd_SoundIsStillPlaying(int handle);
unsigned int snd_GetTick(void);
void *snd_BankLoadByLoc(int sector, int offset);
int snd_GetLastLoadError(void);
int snd_GetFreeSPUDMA(void);
void snd_FreeSPUDMA(int channel);
void snd_KeyOnVoiceRaw(int core, int voice);
void snd_KeyOffVoiceRaw(int core, int voice);

#endif
