#ifndef SCE_IOP_989SND_MIDI_CONTROL_H
#define SCE_IOP_989SND_MIDI_CONTROL_H

#include "sce/iop/types.h"
extern int gGlobalExcite;
void snd_SetMIDIRegister(u_int handle, int reg, short value);
int snd_GetMIDIRegister(u_int handle, int reg);
void snd_SetGlobalExcite(int value);
int snd_GetGlobalExcite(void);
struct MIDIHandler;
struct GenericSoundHandler;
void snd_MIDISoundOwnerProc(int voice, struct GenericSoundHandler* owner, int event);
void snd_SetMIDISoundVolumePan(u_int handle, int volume, int pan);
void snd_SetMIDISoundPitchModifier(u_int handle, short modifier);
void snd_SetAMESoundVolumePan(u_int handle, int volume, int pan);
void snd_SetAMESoundPitchModifier(u_int handle, short modifier);
void snd_SetMIDIHandlerVolumePan(struct MIDIHandler* handler, int volume, int pan);
void snd_SetMIDIHandlerPitchModifier(struct MIDIHandler* handler, short modifier);
#endif
