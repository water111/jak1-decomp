#ifndef SSOUND_H
#define SSOUND_H

#include "overlord/isocommon.h"
#include "overlord/soundcommon.h"

extern int gTrapSRAM;
extern int gStreamSRAM;
extern int gVoice;
extern int gSema;
extern VolumePair gPanTable[361];

void SetBufferMem(void* memory, unsigned int size);
void ReleaseBufferMem(void);
int CalculateFalloffVolume(Vec3w* position, int volume, int curve, int min, int max);
unsigned int CalculateAngle(Vec3w* position);
void InitSound(void);
Sound* LookupSound(int id);
Sound* AllocateSound();
void SetMusicVol(void);
void UpdateVolume(Sound* sound);
int GetVolume(Sound* sound);
int GetPan(Sound* sound);
void KillSoundsInGroup(unsigned char group);
void SetCurve(int curve, int falloff, int ease);
void SetEarTrans(Vec3w* ear, Vec3w* camera, int angle);
void PrintActiveSounds(void);

#endif
