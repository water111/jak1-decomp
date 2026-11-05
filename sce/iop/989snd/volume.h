#ifndef SCE_IOP_989SND_VOLUME_H
#define SCE_IOP_989SND_VOLUME_H

#include "sce/iop/989snd/pantable.h"
#include "sce/iop/types.h"

struct VolPair;

void snd_MakeVolumes(int snd_vol,
                     int snd_pan,
                     int prog_vol,
                     int prog_pan,
                     int tone_vol,
                     int tone_pan,
                     struct VolPair* spu_vol);
void snd_MakeVolumesB(int bank_vol,
                      int snd_vol,
                      int snd_pan,
                      int prog_vol,
                      int prog_pan,
                      int tone_vol,
                      int tone_pan,
                      struct VolPair* spu_vol);
void snd_AdjustAllChannelsMasterVolume(int volume, int group);
void snd_SetMasterVolume(int group, int volume);
int snd_GetMasterVolume(int group);
void snd_SetPlaybackMode(int mode);
int snd_GetPlaybackMode(void);
short snd_MIDITo360Pan(signed char pan);
short snd_AdjustVolToGroup(short volume, int group);
#define SND_MASTER_VOLUME_GROUPS 16
extern int gMasterVol[SND_MASTER_VOLUME_GROUPS];
extern short gStereoOrMono;
extern int gCurrentXAVolGroup;
extern int gCurrentXAVol;
extern int gCurrentXAPan;
#endif
