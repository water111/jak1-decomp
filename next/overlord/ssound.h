#ifndef SSOUND_H
#define SSOUND_H

#include "overlord/isocommon.h"

extern int gTrapSRAM;
extern int gStreamSRAM;
extern int gVoice;
extern VolumePair gPanTable[361];

void SetBufferMem(void* memory, unsigned int size);
void ReleaseBufferMem(void);
int CalculateFalloffVolume(Vec3w* position, int volume, int curve, int min, int max);
unsigned int CalculateAngle(Vec3w* position);
void InitSound(void);

#endif
