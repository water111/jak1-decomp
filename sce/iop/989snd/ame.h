#ifndef SCE_IOP_989SND_AME_H
#define SCE_IOP_989SND_AME_H

#include "sce/iop/989snd/midi.h"
#include "sce/iop/types.h"

struct AMEHandler;
struct AMEStreamHeader;
struct MIDIHandler;
struct MIDISound;

signed char* snd_AMEFunction(struct MIDIHandler* stream, u_char* ame_header);
void snd_SetMIDIRegister(u_int handle, int reg, short value);
int snd_GetMIDIRegister(u_int handle, int reg);
void snd_SetGlobalExcite(int value);
int snd_GetGlobalExcite(void);
u_int snd_PlayAMESound(struct MIDISound* sound, int volume, int pan, short modifier, short bend);
void snd_SetAMESoundVolumePan(u_int handle, int volume, int pan);
void snd_SetAMESoundPitchModifier(u_int handle, short modifier);
int snd_StartAMESegment(struct AMEHandler* parent, int segment);
void snd_StopAMESegment(struct MIDIHandler* handler);
struct AMEStreamHeader* snd_FindAMEStream(struct MIDIHandler* handler, int index);

struct AMEStreamHeader {
  MIDIBlockHeader block;
  struct MIDIHandler handler;
};

#endif
